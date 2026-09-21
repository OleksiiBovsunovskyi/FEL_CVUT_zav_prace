module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <vulkan/vulkan_core.h>

#include <stb_image.h>
#include <stb_image_resize2.h>

#include <bc7enc.h>

module GltfImporter;

import Logger;

namespace fs = std::filesystem;

namespace {

/// Channels every decoded image is expanded to.
constexpr int DECODED_CHANNELS = 4;

glm::mat4 toGlm(const fastgltf::math::fmat4x4& m) {
    glm::mat4 result{1.0f};
    const float* src = m.data();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result[column][row] = src[column * 4 + row];
    return result;
}

/// Loose bounding sphere around the AABB centre.
MeshBounds computeBounds(const std::vector<GPUVertex>& vertices) {
    if (vertices.empty()) return {};

    glm::vec3 min{vertices[0].position[0], vertices[0].position[1],
                  vertices[0].position[2]};
    glm::vec3 max = min;
    for (const GPUVertex& v : vertices) {
        const glm::vec3 p{v.position[0], v.position[1], v.position[2]};
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    MeshBounds bounds{};
    bounds.center = (min + max) * 0.5f;
    for (const GPUVertex& v : vertices) {
        const glm::vec3 p{v.position[0], v.position[1], v.position[2]};
        bounds.radius = std::max(bounds.radius, glm::length(p - bounds.center));
    }
    return bounds;
}

/**
 * glTF renders a primitive without NORMAL flat shaded. Area-weighted: larger
 * triangles contribute more.
 */
void generateNormals(std::vector<GPUVertex>& vertices,
                     const std::vector<uint32_t>& indices) {
    for (GPUVertex& v : vertices)
        v.normal[0] = v.normal[1] = v.normal[2] = 0.0f;

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        GPUVertex& a = vertices[indices[i + 0]];
        GPUVertex& b = vertices[indices[i + 1]];
        GPUVertex& c = vertices[indices[i + 2]];

        const glm::vec3 pa{a.position[0], a.position[1], a.position[2]};
        const glm::vec3 pb{b.position[0], b.position[1], b.position[2]};
        const glm::vec3 pc{c.position[0], c.position[1], c.position[2]};

        const glm::vec3 faceNormal = glm::cross(pb - pa, pc - pa);
        for (GPUVertex* v : {&a, &b, &c}) {
            v->normal[0] += faceNormal.x;
            v->normal[1] += faceNormal.y;
            v->normal[2] += faceNormal.z;
        }
    }

    for (GPUVertex& v : vertices) {
        glm::vec3 n{v.normal[0], v.normal[1], v.normal[2]};
        const float length = glm::length(n);
        n = (length > 0.0f) ? n / length : glm::vec3{0.0f, 1.0f, 0.0f};
        v.normal[0] = n.x;
        v.normal[1] = n.y;
        v.normal[2] = n.z;
    }
}

/// @return the bytes a data source holds itself, empty when it holds none.
std::span<const std::byte> directBytes(const fastgltf::DataSource& source) {
    if (const auto* array = std::get_if<fastgltf::sources::Array>(&source))
        return {array->bytes.data(), array->bytes.size()};
    if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&source))
        return {view->bytes.data(), view->bytes.size()};
    if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&source))
        return {vector->bytes.data(), vector->bytes.size()};
    return {};
}

/**
 * @return one image's encoded PNG or JPEG bytes.
 * @note A .glb keeps them in a buffer view; LoadExternalImages puts a sidecar
 *       file's into an array.
 */
std::span<const std::byte> imageBytes(const fastgltf::Asset& asset,
                                      const fastgltf::Image& image) {
    if (const auto* source = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
        const fastgltf::BufferView& view = asset.bufferViews[source->bufferViewIndex];
        const std::span<const std::byte> buffer =
            directBytes(asset.buffers[view.bufferIndex].data);
        if (view.byteOffset + view.byteLength > buffer.size()) return {};
        return buffer.subspan(view.byteOffset, view.byteLength);
    }
    return directBytes(image.data);
}

/**
 * Builds the whole mip chain from level 0, each level filtered from the one
 * above it, and appends every level after it.
 *
 * @param texture holds level 0 only on entry, the whole chain on return.
 * @note sRGB levels are filtered in linear light and alpha-weighted; a unorm
 *       texture carries numbers, so its channels are averaged as they are.
 */
void generateMips(ImportedAsset::TextureStorage& texture) {
    const bool srgb = texture.format == AssetFormat{VK_FORMAT_R8G8B8A8_SRGB};
    texture.levelCount = assetLevelCount(texture.width, texture.height);

    uint32_t sourceWidth  = texture.width;
    uint32_t sourceHeight = texture.height;
    size_t   sourceOffset = 0;

    for (uint32_t level = 1; level < texture.levelCount; ++level) {
        const uint32_t width  = std::max(sourceWidth / 2, 1u);
        const uint32_t height = std::max(sourceHeight / 2, 1u);

        const size_t destinationOffset = texture.pixels.size();
        texture.pixels.resize(destinationOffset +
                              size_t{width} * height * DECODED_CHANNELS);

        const auto* source = reinterpret_cast<const unsigned char*>(
            texture.pixels.data() + sourceOffset);
        auto* destination = reinterpret_cast<unsigned char*>(texture.pixels.data() +
                                                             destinationOffset);

        if (srgb)
            stbir_resize_uint8_srgb(source, static_cast<int>(sourceWidth),
                                    static_cast<int>(sourceHeight), 0, destination,
                                    static_cast<int>(width), static_cast<int>(height),
                                    0, STBIR_RGBA);
        else
            stbir_resize_uint8_linear(source, static_cast<int>(sourceWidth),
                                      static_cast<int>(sourceHeight), 0, destination,
                                      static_cast<int>(width), static_cast<int>(height),
                                      0, STBIR_4CHANNEL);

        sourceWidth  = width;
        sourceHeight = height;
        sourceOffset = destinationOffset;
    }
}

/// One BC7 block covers 4x4 texels and occupies BC7ENC_BLOCK_SIZE bytes.
constexpr uint32_t BLOCK_TEXELS = 4;

/// @return level n of an image that size, down to 1x1.
std::pair<uint32_t, uint32_t> levelExtent(uint32_t width, uint32_t height,
                                          uint32_t level) {
    return {std::max(width >> level, 1u), std::max(height >> level, 1u)};
}

/**
 * Runs `body(row)` for every row, spread over the hardware threads.
 *
 * @param rows rows to cover; row indices run from zero.
 * @param body called once per row, concurrently.
 */
template <typename Body>
void forEachRow(uint32_t rows, Body body) {
    const uint32_t threads =
        std::min(rows, std::max(std::thread::hardware_concurrency(), 1u));
    if (threads <= 1) {
        for (uint32_t row = 0; row < rows; ++row) body(row);
        return;
    }

    std::vector<std::jthread> workers;
    workers.reserve(threads);
    for (uint32_t start = 0; start < threads; ++start)
        workers.emplace_back([=] {
            for (uint32_t row = start; row < rows; row += threads) body(row);
        });
}

/**
 * Encodes every mip level to BC7, a quarter of the RGBA8 size.
 *
 * @param texture holds an RGBA8 chain on entry, a BC7 chain on return.
 * @note bc7enc_compress_block_init() fills global tables and runs once for the
 *       whole process, before any block is encoded.
 * @note A level whose width or height is not a multiple of four is padded to
 *       whole blocks by repeating its last row and column.
 */
void compressBC7(ImportedAsset::TextureStorage& texture) {
    static const bool initialized = [] {
        bc7enc_compress_block_init();
        return true;
    }();
    (void) initialized;

    const bool srgb = texture.format == AssetFormat{VK_FORMAT_R8G8B8A8_SRGB};

    bc7enc_compress_block_params params{};
    bc7enc_compress_block_params_init(&params);
    if (srgb)
        bc7enc_compress_block_params_init_perceptual_weights(&params);
    else
        bc7enc_compress_block_params_init_linear_weights(&params);

    const AssetFormat compressed{static_cast<uint32_t>(
        srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK)};

    size_t compressedBytes = 0;
    for (uint32_t level = 0; level < texture.levelCount; ++level) {
        const auto [width, height] = levelExtent(texture.width, texture.height, level);
        compressedBytes += assetLevelBytes(compressed, width, height);
    }

    std::vector<std::byte> blocks(compressedBytes);

    size_t sourceOffset = 0;
    size_t blockOffset  = 0;
    for (uint32_t level = 0; level < texture.levelCount; ++level) {
        const auto [width, height] = levelExtent(texture.width, texture.height, level);
        const uint32_t blockColumns = (width + BLOCK_TEXELS - 1) / BLOCK_TEXELS;
        const uint32_t blockRows    = (height + BLOCK_TEXELS - 1) / BLOCK_TEXELS;

        const std::byte* source      = texture.pixels.data() + sourceOffset;
        std::byte*       destination = blocks.data() + blockOffset;

        forEachRow(blockRows, [&](uint32_t blockRow) {
            std::array<uint8_t, BLOCK_TEXELS * BLOCK_TEXELS * DECODED_CHANNELS> texels{};

            for (uint32_t blockColumn = 0; blockColumn < blockColumns; ++blockColumn) {
                for (uint32_t y = 0; y < BLOCK_TEXELS; ++y) {
                    const uint32_t sourceY =
                        std::min(blockRow * BLOCK_TEXELS + y, height - 1);
                    for (uint32_t x = 0; x < BLOCK_TEXELS; ++x) {
                        const uint32_t sourceX =
                            std::min(blockColumn * BLOCK_TEXELS + x, width - 1);
                        std::memcpy(
                            texels.data() + (y * BLOCK_TEXELS + x) * DECODED_CHANNELS,
                            source + (size_t{sourceY} * width + sourceX) * DECODED_CHANNELS,
                            DECODED_CHANNELS);
                    }
                }

                bc7enc_compress_block(
                    destination +
                        (size_t{blockRow} * blockColumns + blockColumn) * BC7ENC_BLOCK_SIZE,
                    texels.data(), &params);
            }
        });

        sourceOffset += assetLevelBytes(texture.format, width, height);
        blockOffset  += size_t{blockRows} * blockColumns * BC7ENC_BLOCK_SIZE;
    }

    texture.pixels = std::move(blocks);
    texture.format = compressed;
}

AssetMaterial makeMaterial(const fastgltf::Material& source) {
    AssetMaterial material{};

    const auto& pbr = source.pbrData;
    material.albedo = glm::vec4{
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2]),
        static_cast<float>(pbr.baseColorFactor[3]),
    };
    material.metallic  = static_cast<float>(pbr.metallicFactor);
    material.roughness = static_cast<float>(pbr.roughnessFactor);

    material.emissiveColor = glm::vec3{
        static_cast<float>(source.emissiveFactor[0]),
        static_cast<float>(source.emissiveFactor[1]),
        static_cast<float>(source.emissiveFactor[2]),
    };
    if (material.emissiveColor != glm::vec3{0.0f})
        material.flags |= MATERIAL_EMISSIVE;

    switch (source.alphaMode) {
        case fastgltf::AlphaMode::Blend:
            material.flags |= MATERIAL_ALPHA_BLEND;
            break;
        case fastgltf::AlphaMode::Mask:
            material.alphaThreshold = static_cast<float>(source.alphaCutoff);
            material.flags |= MATERIAL_ALPHA_MASK;
            break;
        case fastgltf::AlphaMode::Opaque:
            break;
    }
    return material;
}

/// Reads one primitive's attributes into the interleaved GPU layout.
bool readVertices(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                  std::vector<GPUVertex>& vertices) {
    const auto* positionAttribute = primitive.findAttribute("POSITION");
    if (positionAttribute == primitive.attributes.end()) {
        logError("GltfImporter: primitive has no POSITION attribute");
        return false;
    }

    const auto& positions = asset.accessors[positionAttribute->accessorIndex];
    if (positions.count == 0) return false;

    vertices.assign(positions.count, GPUVertex{});

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
        asset, positions, [&](fastgltf::math::fvec3 p, size_t i) {
            vertices[i].position[0] = p[0];
            vertices[i].position[1] = p[1];
            vertices[i].position[2] = p[2];
            vertices[i].position[3] = 1.0f;
        });

    if (const auto* it = primitive.findAttribute("NORMAL");
        it != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset, asset.accessors[it->accessorIndex],
            [&](fastgltf::math::fvec3 n, size_t i) {
                vertices[i].normal[0] = n[0];
                vertices[i].normal[1] = n[1];
                vertices[i].normal[2] = n[2];
            });
    }

    if (const auto* it = primitive.findAttribute("TANGENT");
        it != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
            asset, asset.accessors[it->accessorIndex],
            [&](fastgltf::math::fvec4 t, size_t i) {
                vertices[i].tangent[0] = t[0];
                vertices[i].tangent[1] = t[1];
                vertices[i].tangent[2] = t[2];
                vertices[i].tangent[3] = t[3];   /* handedness */
            });
    }

    if (const auto* it = primitive.findAttribute("TEXCOORD_0");
        it != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset, asset.accessors[it->accessorIndex],
            [&](fastgltf::math::fvec2 uv, size_t i) {
                vertices[i].texCoord[0] = uv[0];
                vertices[i].texCoord[1] = uv[1];
            });
    }

    return true;
}

bool readIndices(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
                 std::vector<uint32_t>& indices) {
    /* Options::GenerateMeshIndices gives even non-indexed primitives one. */
    if (!primitive.indicesAccessor.has_value()) {
        logError("GltfImporter: primitive has no indices");
        return false;
    }

    const auto& accessor = asset.accessors[*primitive.indicesAccessor];
    if (accessor.count == 0 || accessor.count % 3 != 0) {
        logError("GltfImporter: index count " + std::to_string(accessor.count) +
                 " is not a whole number of triangles");
        return false;
    }

    indices.resize(accessor.count);
    fastgltf::copyFromAccessor<uint32_t>(asset, accessor, indices.data());
    return true;
}

} // namespace

bool importGltf(const fs::path& path, const GltfImportSettings& settings,
                ImportedAsset& out) {
    const std::string extension = path.extension().string();
    if (extension != ".gltf" && extension != ".glb") {
        logError("GltfImporter: " + path.string() + " is not .gltf or .glb");
        return false;
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        logError("GltfImporter: cannot read " + path.string() + ": " +
                 std::string(fastgltf::getErrorMessage(data.error())));
        return false;
    }

    // LoadExternalBuffers covers .gltf with sidecar .bin files, LoadExternalImages
    // the .png and .jpeg beside them; GenerateMeshIndices saves handling
    // non-indexed primitives separately.
    constexpr auto options = fastgltf::Options::LoadExternalBuffers |
                             fastgltf::Options::LoadExternalImages |
                             fastgltf::Options::GenerateMeshIndices;

    /**
     * KHR_mesh_quantization is handled by the accessor conversion below; the
     * texture and material ones only affect appearance nothing reads yet.
     *
     * EXT_meshopt_compression is absent on purpose: fastgltf parses its
     * metadata but never calls meshopt_decode, and would yield compressed bytes.
     */
    constexpr auto extensions = fastgltf::Extensions::KHR_mesh_quantization |
                                fastgltf::Extensions::KHR_texture_transform |
                                fastgltf::Extensions::KHR_texture_basisu |
                                fastgltf::Extensions::KHR_lights_punctual |
                                fastgltf::Extensions::KHR_materials_emissive_strength |
                                fastgltf::Extensions::KHR_materials_specular |
                                fastgltf::Extensions::KHR_materials_ior |
                                fastgltf::Extensions::KHR_materials_clearcoat |
                                fastgltf::Extensions::KHR_materials_transmission |
                                fastgltf::Extensions::KHR_materials_volume;

    fastgltf::Parser parser{extensions};
    auto parsed = parser.loadGltf(data.get(), path.parent_path(), options);
    if (parsed.error() != fastgltf::Error::None) {
        logError("GltfImporter: cannot parse " + path.string() + ": " +
                 std::string(fastgltf::getErrorMessage(parsed.error())));
        if (parsed.error() == fastgltf::Error::MissingExtensions) {
            logError("GltfImporter: if this came out of gltfpack it is probably "
                     "EXT_meshopt_compression, which needs decompressing before "
                     "it can be read. Re-export without -c.");
        }
        return false;
    }
    const fastgltf::Asset& asset = parsed.get();

    /* In case a compressed bufferView slips past the extension list. */
    for (const fastgltf::BufferView& view : asset.bufferViews) {
        if (view.meshoptCompression != nullptr) {
            logError("GltfImporter: " + path.string() +
                     " has meshopt-compressed buffer views, which are not decoded");
            return false;
        }
    }

    /* Indexed by glTF material index; the last slot covers primitives that name
     * no material and is filled on demand. */
    std::vector<AssetMaterialIndex> materials(asset.materials.size());
    AssetMaterialIndex              defaultMaterial{};

    /* [glTF mesh][primitive] -> slot in out.meshes. */
    std::vector<std::vector<AssetMeshIndex>> meshCache(asset.meshes.size());
    std::vector<std::vector<bool>>           meshCached(asset.meshes.size());

    /* (glTF image, sRGB) -> slot in out.textures. Albedo and emissive are colour
     * and decode as sRGB; normal and ORM carry numbers and must not, so one
     * image sampled both ways occupies two slots. */
    std::unordered_map<uint64_t, AssetTextureIndex> textureSlots;

    auto slotForTexture = [&](size_t textureIndex, bool srgb) -> AssetTextureIndex {
        if (textureIndex >= asset.textures.size()) return {};

        const fastgltf::Texture& texture = asset.textures[textureIndex];
        if (!texture.imageIndex.has_value()) {
            logError("GltfImporter: texture " + std::to_string(textureIndex) +
                     " names no plain image; a KHR_texture_basisu source is parsed "
                     "but not decoded");
            return {};
        }
        const size_t   imageIndex = *texture.imageIndex;
        const uint64_t key =
            (static_cast<uint64_t>(imageIndex) << 1) | (srgb ? 1ull : 0ull);
        if (const auto it = textureSlots.find(key); it != textureSlots.end())
            return it->second;

        const std::span<const std::byte> encoded =
            imageBytes(asset, asset.images[imageIndex]);
        if (encoded.empty()) {
            logError("GltfImporter: image " + std::to_string(imageIndex) +
                     " has no bytes to decode");
            return {};
        }

        int width = 0, height = 0, sourceChannels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(encoded.data()),
            static_cast<int>(encoded.size()), &width, &height, &sourceChannels,
            STBI_rgb_alpha);
        if (!pixels) {
            logError("GltfImporter: cannot decode image " +
                     std::to_string(imageIndex) + ": " + stbi_failure_reason());
            return {};
        }

        ImportedAsset::TextureStorage storage{};
        storage.width  = static_cast<uint32_t>(width);
        storage.height = static_cast<uint32_t>(height);
        storage.format = AssetFormat{static_cast<uint32_t>(
            srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM)};

        const auto* bytes = reinterpret_cast<const std::byte*>(pixels);
        storage.pixels.assign(
            bytes, bytes + size_t{storage.width} * storage.height * DECODED_CHANNELS);
        stbi_image_free(pixels);

        generateMips(storage);
        compressBC7(storage);

        const AssetTextureIndex slot{static_cast<uint32_t>(out.textures.size())};
        out.textures.push_back(std::move(storage));
        textureSlots.emplace(key, slot);
        return slot;
    };

    auto materialFor = [&](const fastgltf::Primitive& primitive) -> AssetMaterialIndex {
        if (!primitive.materialIndex.has_value()) {
            if (!defaultMaterial) {
                defaultMaterial =
                    AssetMaterialIndex{static_cast<uint32_t>(out.materials.size())};
                out.materials.emplace_back();
            }
            return defaultMaterial;
        }
        const size_t index = *primitive.materialIndex;
        if (materials[index]) return materials[index];

        const fastgltf::Material& source = asset.materials[index];
        AssetMaterial             material = makeMaterial(source);

        if (source.pbrData.baseColorTexture.has_value())
            material.textures[0] =
                slotForTexture(source.pbrData.baseColorTexture->textureIndex, true);
        if (source.normalTexture.has_value())
            material.textures[1] =
                slotForTexture(source.normalTexture->textureIndex, false);
        if (source.pbrData.metallicRoughnessTexture.has_value())
            material.textures[2] = slotForTexture(
                source.pbrData.metallicRoughnessTexture->textureIndex, false);
        if (source.emissiveTexture.has_value())
            material.textures[3] =
                slotForTexture(source.emissiveTexture->textureIndex, true);

        materials[index] = AssetMaterialIndex{static_cast<uint32_t>(out.materials.size())};
        out.materials.push_back(material);
        return materials[index];
    };

    std::vector<GPUVertex> vertices;
    std::vector<uint32_t>  indices;

    /// Builds one primitive, or returns the slot it already occupies.
    auto meshFor = [&](size_t meshIndex,
                       size_t primitiveIndex) -> std::optional<AssetMeshIndex> {
        auto& cache  = meshCache[meshIndex];
        auto& cached = meshCached[meshIndex];
        if (cache.size() <= primitiveIndex) {
            cache.resize(primitiveIndex + 1);
            cached.resize(primitiveIndex + 1, false);
        }
        if (cached[primitiveIndex]) return cache[primitiveIndex];

        const fastgltf::Primitive& primitive =
            asset.meshes[meshIndex].primitives[primitiveIndex];

        if (primitive.type != fastgltf::PrimitiveType::Triangles) {
            logError("GltfImporter: skipping non-triangle primitive in mesh " +
                     std::to_string(meshIndex));
            return std::nullopt;
        }
        if (!readVertices(asset, primitive, vertices)) return std::nullopt;
        if (!readIndices(asset, primitive, indices)) return std::nullopt;

        const auto* normalAttribute = primitive.findAttribute("NORMAL");
        if (normalAttribute == primitive.attributes.end())
            generateNormals(vertices, indices);

        ImportedAsset::MeshStorage storage{};
        if (!generateClusterLOD(vertices, indices, settings.clusterLod, storage.lod)) {
            logError("GltfImporter: meshlet generation failed for mesh " +
                     std::to_string(meshIndex) + " primitive " +
                     std::to_string(primitiveIndex));
            return std::nullopt;
        }
        storage.bounds   = computeBounds(vertices);
        storage.material = materialFor(primitive);
        storage.vertices = vertices;

        const AssetMeshIndex slot{static_cast<uint32_t>(out.meshes.size())};
        out.meshes.push_back(std::move(storage));

        cache[primitiveIndex]  = slot;
        cached[primitiveIndex] = true;
        return slot;
    };

    auto addNode = [&](size_t meshIndex, const glm::mat4& transform) {
        for (size_t p = 0; p < asset.meshes[meshIndex].primitives.size(); ++p) {
            if (const std::optional<AssetMeshIndex> slot = meshFor(meshIndex, p))
                out.parts.push_back(AssetPart{*slot, transform});
        }
    };

    if (asset.scenes.empty()) {
        /* An asset may carry meshes with no scene. */
        for (size_t m = 0; m < asset.meshes.size(); ++m)
            addNode(m, glm::mat4{1.0f});
    } else {
        const size_t sceneIndex = asset.defaultScene.value_or(0);
        fastgltf::iterateSceneNodes(
            asset, sceneIndex, fastgltf::math::fmat4x4{},
            [&](const fastgltf::Node& node, const fastgltf::math::fmat4x4& world) {
                if (node.meshIndex.has_value())
                    addNode(*node.meshIndex, toGlm(world));
            });
    }

    if (out.parts.empty()) {
        logError("GltfImporter: " + path.string() + " produced no drawable meshes");
        return false;
    }

    logMessage("GltfImporter: " + path.filename().string() + " -> " +
               std::to_string(out.meshes.size()) + " meshes, " +
               std::to_string(out.materials.size()) + " materials, " +
               std::to_string(out.textures.size()) + " textures, " +
               std::to_string(out.parts.size()) + " parts");
    return true;
}
