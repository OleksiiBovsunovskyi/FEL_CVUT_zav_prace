module;

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

module GltfLoader;

import vulkan;
import Logger;

namespace fs = std::filesystem;

namespace {

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

    glm::vec3 min{vertices[0].position[0], vertices[0].position[1], vertices[0].position[2]};
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

std::shared_ptr<Material> makeMaterial(const fastgltf::Material& source) {
    auto material = std::make_shared<Material>();

    const auto& pbr = source.pbrData;
    material->setAlbedo(glm::vec4{
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2]),
        static_cast<float>(pbr.baseColorFactor[3]),
    });
    material->setMetallic(static_cast<float>(pbr.metallicFactor));
    material->setRoughness(static_cast<float>(pbr.roughnessFactor));

    const glm::vec3 emissive{
        static_cast<float>(source.emissiveFactor[0]),
        static_cast<float>(source.emissiveFactor[1]),
        static_cast<float>(source.emissiveFactor[2]),
    };
    material->setEmissiveColor(emissive);
    material->setEmissive(emissive != glm::vec3{0.0f});

    switch (source.alphaMode) {
        case fastgltf::AlphaMode::Blend:
            material->setTransparent(true);
            break;
        case fastgltf::AlphaMode::Mask:
            material->setAlphaThreshold(static_cast<float>(source.alphaCutoff));
            break;
        case fastgltf::AlphaMode::Opaque:
            break;
    }

    /* Textures unset: they need a bindless descriptor array. */
    return material;
}

/// Reads one primitive's attributes into the interleaved GPU layout.
bool readVertices(const fastgltf::Asset& asset,
                  const fastgltf::Primitive& primitive,
                  std::vector<GPUVertex>& vertices) {
    const auto* positionAttribute = primitive.findAttribute("POSITION");
    if (positionAttribute == primitive.attributes.end()) {
        logError("GltfLoader: primitive has no POSITION attribute");
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

bool readIndices(const fastgltf::Asset& asset,
                 const fastgltf::Primitive& primitive,
                 std::vector<uint32_t>& indices) {
    /* Options::GenerateMeshIndices gives even non-indexed primitives one. */
    if (!primitive.indicesAccessor.has_value()) {
        logError("GltfLoader: primitive has no indices");
        return false;
    }

    const auto& accessor = asset.accessors[*primitive.indicesAccessor];
    if (accessor.count == 0 || accessor.count % 3 != 0) {
        logError("GltfLoader: index count " + std::to_string(accessor.count) +
                 " is not a whole number of triangles");
        return false;
    }

    indices.resize(accessor.count);
    fastgltf::copyFromAccessor<uint32_t>(asset, accessor, indices.data());
    return true;
}

} // namespace

bool GltfLoader::init(BufferManager& buffers, BlockingTransferBatch& batch) {
    if (!buffers.initialized()) {
        logError("GltfLoader: BufferManager must be initialized first");
        return false;
    }
    buffers_ = &buffers;
    batch_   = &batch;
    return true;
}

std::shared_ptr<MultiMesh> GltfLoader::loadModel(const fs::path& path,
                                                 const GltfLoadSettings& settings) {
    if (!buffers_ || !batch_) {
        logError("GltfLoader: loadModel called before init");
        return nullptr;
    }
    BufferManager&         buffers = *buffers_;
    BlockingTransferBatch& batch   = *batch_;

    const std::string extension = path.extension().string();
    if (extension != ".gltf" && extension != ".glb") {
        logError("GltfLoader: " + path.string() + " is not .gltf or .glb");
        return nullptr;
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        logError("GltfLoader: cannot read " + path.string() + ": " +
                 std::string(fastgltf::getErrorMessage(data.error())));
        return nullptr;
    }

    // LoadExternalBuffers covers .gltf with sidecar .bin files; GenerateMeshIndices
    // saves handling non-indexed primitives separately.
    constexpr auto options = fastgltf::Options::LoadExternalBuffers |
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
        logError("GltfLoader: cannot parse " + path.string() + ": " +
                 std::string(fastgltf::getErrorMessage(parsed.error())));
        if (parsed.error() == fastgltf::Error::MissingExtensions) {
            logError("GltfLoader: if this came out of gltfpack it is probably "
                     "EXT_meshopt_compression, which needs decompressing before "
                     "it can be read. Re-export without -c.");
        }
        return nullptr;
    }
    const fastgltf::Asset& asset = parsed.get();

    /* In case a compressed bufferView slips past the extension list. */
    for (const fastgltf::BufferView& view : asset.bufferViews) {
        if (view.meshoptCompression != nullptr) {
            logError("GltfLoader: " + path.string() +
                     " has meshopt-compressed buffer views, which are not decoded");
            return nullptr;
        }
    }

    /* Indexed by glTF material index; defaultMaterial covers the rest. */
    std::vector<std::shared_ptr<Material>> materials(asset.materials.size());
    std::shared_ptr<Material> defaultMaterial;

    // [glTF mesh][primitive] -> uploaded geometry.
    std::vector<std::vector<std::shared_ptr<Mesh>>> meshCache(asset.meshes.size());

    // [glTF mesh] -> its parts, shared by every node referencing it.
    std::vector<std::shared_ptr<MultiMesh>> multiMeshCache(asset.meshes.size());

    std::vector<GPUVertex> vertices;
    std::vector<uint32_t>  indices;
    GeneratedClusterLOD    clusterLod;

    auto materialFor = [&](const fastgltf::Primitive& primitive)
        -> std::shared_ptr<Material> {
        if (!primitive.materialIndex.has_value()) {
            if (!defaultMaterial) defaultMaterial = std::make_shared<Material>();
            return defaultMaterial;
        }
        const size_t index = *primitive.materialIndex;
        if (!materials[index]) materials[index] = makeMaterial(asset.materials[index]);
        return materials[index];
    };

    /// Uploads one primitive, or returns the already-uploaded Mesh.
    auto meshFor = [&](size_t meshIndex, size_t primitiveIndex)
        -> std::shared_ptr<Mesh> {
        auto& cache = meshCache[meshIndex];
        if (cache.size() <= primitiveIndex) cache.resize(primitiveIndex + 1);
        if (cache[primitiveIndex]) return cache[primitiveIndex];

        const fastgltf::Primitive& primitive =
            asset.meshes[meshIndex].primitives[primitiveIndex];

        if (primitive.type != fastgltf::PrimitiveType::Triangles) {
            logError("GltfLoader: skipping non-triangle primitive in mesh " +
                     std::to_string(meshIndex));
            return nullptr;
        }
        if (!readVertices(asset, primitive, vertices)) return nullptr;
        if (!readIndices(asset, primitive, indices))   return nullptr;

        const auto* normalAttribute = primitive.findAttribute("NORMAL");
        if (normalAttribute == primitive.attributes.end())
            generateNormals(vertices, indices);

        if (!generateClusterLOD(vertices, indices, settings.clusterLod, clusterLod)) {
            logError("GltfLoader: meshlet generation failed for mesh " +
                     std::to_string(meshIndex) + " primitive " +
                     std::to_string(primitiveIndex));
            return nullptr;
        }

        /**
         * One batch per primitive: a GPU round trip each, with the upload buffer empty on
         * entry so a large primitive is never starved by its predecessors.
         */
        vk::CommandBuffer cmd = batch.begin();
        if (!cmd) return nullptr;

        auto mesh = std::make_shared<Mesh>();
        const bool uploaded = mesh->upload(
            buffers, cmd,
            clusterLod.uploadData(vertices, computeBounds(vertices)),
            materialFor(primitive));

        if (!batch.submitAndWait()) return nullptr;
        buffers.resetUpload();
        if (!uploaded) {
            logError("GltfLoader: upload failed for mesh " + std::to_string(meshIndex) +
                     " primitive " + std::to_string(primitiveIndex));
            return nullptr;
        }

        cache[primitiveIndex] = std::move(mesh);
        return cache[primitiveIndex];
    };

    /// Builds one glTF mesh's parts, or returns the already-built ones.
    auto multiMeshFor = [&](size_t meshIndex) -> std::shared_ptr<MultiMesh> {
        if (multiMeshCache[meshIndex]) return multiMeshCache[meshIndex];

        auto multiMesh = std::make_shared<MultiMesh>();
        for (size_t p = 0; p < asset.meshes[meshIndex].primitives.size(); ++p) {
            /* Identity: a glTF primitive has no transform of its own. */
            if (auto mesh = meshFor(meshIndex, p)) multiMesh->add(std::move(mesh));
        }
        if (multiMesh->empty()) return nullptr;

        multiMeshCache[meshIndex] = std::move(multiMesh);
        return multiMeshCache[meshIndex];
    };

    /* One model: every placed node contributes its parts at that node's world
     * transform, so the file becomes one thing to put in the world. */
    auto model = std::make_shared<MultiMesh>();

    auto addInstance = [&](size_t meshIndex, const glm::mat4& transform) {
        const std::shared_ptr<MultiMesh> placed = multiMeshFor(meshIndex);
        if (!placed) return;
        for (const MultiMeshPart& part : placed->parts())
            model->add(part.mesh, transform * part.localTransform);
    };

    if (asset.scenes.empty()) {
        /* An asset may carry meshes with no scene. */
        for (size_t m = 0; m < asset.meshes.size(); ++m)
            addInstance(m, glm::mat4{1.0f});
    } else {
        const size_t sceneIndex = asset.defaultScene.value_or(0);
        fastgltf::iterateSceneNodes(
            asset, sceneIndex, fastgltf::math::fmat4x4{},
            [&](const fastgltf::Node& node, const fastgltf::math::fmat4x4& world) {
                if (node.meshIndex.has_value())
                    addInstance(*node.meshIndex, toGlm(world));
            });
    }

    if (model->empty()) {
        logError("GltfLoader: " + path.string() + " produced no drawable meshes");
        return nullptr;
    }

    logMessage("GltfLoader: " + path.filename().string() + " -> one model, " +
               std::to_string(model->size()) + " parts");
    return model;
}
