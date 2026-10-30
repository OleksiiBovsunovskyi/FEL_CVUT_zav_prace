module;

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

module PmmaFile;

import Logger;

namespace fs = std::filesystem;

namespace {

/// "PMMA" as little-endian bytes.
constexpr uint32_t PMMA_MAGIC   = 0x414d4d50;
constexpr uint32_t PMMA_VERSION = 2;
    
constexpr uint64_t SECTION_ALIGNMENT = 16;

constexpr uint64_t alignUp(uint64_t value) {
    return (value + SECTION_ALIGNMENT - 1) & ~(SECTION_ALIGNMENT - 1);
}

/// One array inside the blob.
struct PmmaRange {
    uint64_t offset   = 0;
    uint32_t count    = 0;
    uint32_t _padding = 0;
};

struct PmmaHeader {
    uint32_t magic         = PMMA_MAGIC;
    uint32_t version       = PMMA_VERSION;
    uint32_t meshCount     = 0;
    uint32_t materialCount = 0;
    uint32_t textureCount  = 0;
    uint32_t partCount     = 0;
    uint64_t blobOffset    = 0;
    uint64_t blobBytes     = 0;
};

struct PmmaMesh {
    PmmaRange vertices;
    PmmaRange meshlets;
    PmmaRange meshletVertexIndices;
    PmmaRange meshletTriangles;
    PmmaRange clusters;
    PmmaRange clusterGroups;
    /// center.xyz, radius.
    glm::vec4 bounds{};
    uint32_t  material = AssetMaterialIndex::NONE;
    uint32_t  _padding[3]{};
};

struct PmmaTexture {
    uint32_t width      = 0;
    uint32_t height     = 0;
    uint32_t format     = 0;
    uint32_t levelCount = 1;
    uint64_t offset     = 0;
    uint64_t bytes      = 0;
};

struct PmmaPart {
    glm::mat4 transform{1.0f};
    uint32_t  mesh = 0;
    uint32_t  _padding[3]{};
};


static_assert(sizeof(PmmaHeader) == 40);
static_assert(sizeof(PmmaRange) == 16);
static_assert(sizeof(PmmaMesh) == 128);
static_assert(sizeof(PmmaTexture) == 32);
static_assert(sizeof(PmmaPart) == 80);
static_assert(sizeof(AssetMaterial) == 72);
static_assert(std::is_trivially_copyable_v<AssetMaterial>);

/// Appends one array to the blob being built, 16-byte aligned.
template <typename T>
PmmaRange appendSection(std::vector<std::byte>& blob, std::span<const T> values) {
    PmmaRange range{};
    range.count = static_cast<uint32_t>(values.size());
    if (values.empty()) return range;

    blob.resize(alignUp(blob.size()));
    range.offset = blob.size();
    const std::byte* bytes = reinterpret_cast<const std::byte*>(values.data());
    blob.insert(blob.end(), bytes, bytes + values.size() * sizeof(T));
    return range;
}

template <typename T>
void appendRecords(std::vector<std::byte>& out, const std::vector<T>& values) {
    if (values.empty()) return;
    const std::byte* bytes = reinterpret_cast<const std::byte*>(values.data());
    out.insert(out.end(), bytes, bytes + values.size() * sizeof(T));
}

/**
 * @return a span over one blob section, or an empty one after logging when the
 *         range is misaligned or does not fit.
 */
template <typename T>
std::span<const T> sectionOf(std::span<const std::byte> blob, const PmmaRange& range,
                             const char* what) {
    if (range.count == 0) return {};

    const uint64_t bytes = uint64_t{range.count} * sizeof(T);
    if (range.offset % SECTION_ALIGNMENT != 0 || range.offset > blob.size() ||
        bytes > blob.size() - range.offset) {
        logError(std::string("PmmaAsset: ") + what + " does not fit the file");
        return {};
    }
    return std::span<const T>(reinterpret_cast<const T*>(blob.data() + range.offset),
                              range.count);
}

/// Reads one packed table out of the file bytes.
template <typename T>
bool readTable(std::span<const std::byte> file, uint64_t& cursor, uint32_t count,
               std::vector<T>& out, const char* what) {
    const uint64_t bytes = uint64_t{count} * sizeof(T);
    if (cursor > file.size() || bytes > file.size() - cursor) {
        logError(std::string("PmmaAsset: the ") + what + " table runs past the file");
        return false;
    }
    out.resize(count);
    if (count != 0) std::memcpy(out.data(), file.data() + cursor, bytes);
    cursor += bytes;
    return true;
}

} // namespace

bool writePmma(const fs::path& path, const AssetModel& model) {
    if (model.meshes.empty() || model.parts.empty()) {
        logError("writePmma: " + path.string() + " has no meshes or no parts");
        return false;
    }

    std::vector<std::byte> blob;
    std::vector<PmmaMesh>  meshTable;
    meshTable.reserve(model.meshes.size());

    for (const AssetMesh& mesh : model.meshes) {
        const MeshUploadData& geometry = mesh.geometry;
        PmmaMesh record{};
        record.vertices             = appendSection(blob, geometry.vertices);
        record.meshlets             = appendSection(blob, geometry.meshlets);
        record.meshletVertexIndices = appendSection(blob, geometry.meshletVertexIndices);
        record.meshletTriangles     = appendSection(blob, geometry.meshletTriangles);
        record.clusters             = appendSection(blob, geometry.clusters);
        record.clusterGroups        = appendSection(blob, geometry.clusterGroups);
        record.bounds   = glm::vec4(geometry.bounds.center, geometry.bounds.radius);
        record.material = mesh.material.value;
        meshTable.push_back(record);
    }

    std::vector<PmmaTexture> textureTable;
    textureTable.reserve(model.textures.size());
    for (const AssetTexture& texture : model.textures) {
        blob.resize(alignUp(blob.size()));
        PmmaTexture record{};
        record.width      = texture.width;
        record.height     = texture.height;
        record.format     = texture.format.value;
        record.levelCount = texture.levelCount;
        record.offset     = blob.size();
        record.bytes      = texture.pixels.size();
        blob.insert(blob.end(), texture.pixels.begin(), texture.pixels.end());
        textureTable.push_back(record);
    }

    std::vector<PmmaPart> partTable;
    partTable.reserve(model.parts.size());
    for (const AssetPart& part : model.parts)
        partTable.push_back(PmmaPart{part.transform, part.mesh.value, {}});

    const std::vector<AssetMaterial> materialTable(model.materials.begin(),
                                                   model.materials.end());

    PmmaHeader header{};
    header.meshCount     = static_cast<uint32_t>(meshTable.size());
    header.materialCount = static_cast<uint32_t>(materialTable.size());
    header.textureCount  = static_cast<uint32_t>(textureTable.size());
    header.partCount     = static_cast<uint32_t>(partTable.size());
    header.blobOffset    = alignUp(sizeof(PmmaHeader) +
                                   meshTable.size() * sizeof(PmmaMesh) +
                                   materialTable.size() * sizeof(AssetMaterial) +
                                   textureTable.size() * sizeof(PmmaTexture) +
                                   partTable.size() * sizeof(PmmaPart));
    header.blobBytes     = blob.size();

    std::vector<std::byte> file;
    file.reserve(header.blobOffset + blob.size());
    const std::byte* headerBytes = reinterpret_cast<const std::byte*>(&header);
    file.insert(file.end(), headerBytes, headerBytes + sizeof(header));
    appendRecords(file, meshTable);
    appendRecords(file, materialTable);
    appendRecords(file, textureTable);
    appendRecords(file, partTable);
    file.resize(header.blobOffset);
    file.insert(file.end(), blob.begin(), blob.end());

    std::error_code error;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), error);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        logError("writePmma: cannot open " + path.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    if (!out) {
        logError("writePmma: cannot write " + path.string());
        return false;
    }
    return true;
}

bool PmmaAsset::read(const fs::path& path) {
    blob_.clear();
    meshes_.clear();
    materials_.clear();
    textures_.clear();
    parts_.clear();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        logError("PmmaAsset: cannot open " + path.string());
        return false;
    }
    const std::streamoff size = in.tellg();
    in.seekg(0);
    if (size < static_cast<std::streamoff>(sizeof(PmmaHeader))) {
        logError("PmmaAsset: " + path.string() + " is too small to hold a header");
        return false;
    }

    std::vector<std::byte> file(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(file.data()), size);
    if (!in) {
        logError("PmmaAsset: cannot read " + path.string());
        return false;
    }

    PmmaHeader header{};
    std::memcpy(&header, file.data(), sizeof(header));
    if (header.magic != PMMA_MAGIC) {
        logError("PmmaAsset: " + path.string() + " is not a .pmma");
        return false;
    }
    if (header.version != PMMA_VERSION) {
        logError("PmmaAsset: " + path.string() + " is version " +
                 std::to_string(header.version) + ", this build reads version " +
                 std::to_string(PMMA_VERSION) + ". Rebake it.");
        return false;
    }
    if (header.blobOffset > file.size() ||
        header.blobBytes > file.size() - header.blobOffset) {
        logError("PmmaAsset: " + path.string() + " is truncated");
        return false;
    }

    std::vector<PmmaMesh>    meshTable;
    std::vector<PmmaTexture> textureTable;
    std::vector<PmmaPart>    partTable;

    uint64_t cursor = sizeof(PmmaHeader);
    if (!readTable(file, cursor, header.meshCount, meshTable, "mesh") ||
        !readTable(file, cursor, header.materialCount, materials_, "material") ||
        !readTable(file, cursor, header.textureCount, textureTable, "texture") ||
        !readTable(file, cursor, header.partCount, partTable, "part"))
        return false;

    blob_.assign(
        file.begin() + static_cast<ptrdiff_t>(header.blobOffset),
        file.begin() + static_cast<ptrdiff_t>(header.blobOffset + header.blobBytes));
    const std::span<const std::byte> blob = blob_;

    meshes_.reserve(meshTable.size());
    for (const PmmaMesh& record : meshTable) {
        if (record.material != AssetMaterialIndex::NONE &&
            record.material >= header.materialCount) {
            logError("PmmaAsset: a mesh names material " +
                     std::to_string(record.material) +
                     ", which the file does not hold");
            return false;
        }

        MeshUploadData geometry{};
        geometry.vertices = sectionOf<GPUVertex>(blob, record.vertices, "vertices");
        geometry.meshlets = sectionOf<GPUMeshlet>(blob, record.meshlets, "meshlets");
        geometry.meshletVertexIndices = sectionOf<uint32_t>(
            blob, record.meshletVertexIndices, "meshlet vertex indices");
        geometry.meshletTriangles =
            sectionOf<uint32_t>(blob, record.meshletTriangles, "meshlet triangles");
        geometry.clusters = sectionOf<GPUCluster>(blob, record.clusters, "clusters");
        geometry.clusterGroups =
            sectionOf<GPUClusterGroup>(blob, record.clusterGroups, "cluster groups");
        geometry.bounds = MeshBounds{glm::vec3(record.bounds), record.bounds.w};

        if (geometry.vertices.empty() || geometry.meshlets.empty() ||
            geometry.meshletVertexIndices.empty() || geometry.meshletTriangles.empty())
            return false;

        meshes_.push_back(AssetMesh{geometry, AssetMaterialIndex{record.material}});
    }

    for (const AssetMaterial& material : materials_) {
        for (const AssetTextureIndex& texture : material.textures) {
            if (texture && texture.value >= header.textureCount) {
                logError("PmmaAsset: a material names texture " +
                         std::to_string(texture.value) +
                         ", which the file does not hold");
                return false;
            }
        }
    }

    textures_.reserve(textureTable.size());
    for (const PmmaTexture& record : textureTable) {
        if (record.offset % SECTION_ALIGNMENT != 0 || record.offset > blob.size() ||
            record.bytes > blob.size() - record.offset) {
            logError("PmmaAsset: a texture's pixels do not fit the file");
            return false;
        }
        textures_.push_back(AssetTexture{record.width, record.height,
                                         AssetFormat{record.format},
                                         record.levelCount,
                                         blob.subspan(record.offset, record.bytes)});
    }

    parts_.reserve(partTable.size());
    for (const PmmaPart& record : partTable) {
        if (record.mesh >= header.meshCount) {
            logError("PmmaAsset: a part names mesh " + std::to_string(record.mesh) +
                     ", which the file does not hold");
            return false;
        }
        parts_.push_back(AssetPart{AssetMeshIndex{record.mesh}, record.transform});
    }

    if (parts_.empty()) {
        logError("PmmaAsset: " + path.string() + " holds no parts");
        return false;
    }
    return true;
}
