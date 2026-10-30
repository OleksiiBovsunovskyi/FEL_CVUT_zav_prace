module;

#include <filesystem>

export module GltfImporter;

export import AssetData;

export struct GltfImportSettings {
    /**
     * Passed straight to generateClusterLOD. The default (enabled = false) builds
     * meshlets with one terminal group; the layout matches hierarchical mode.
     */
    ClusterLODSettings clusterLod{};
};

/**
 * Reads a .gltf or .glb into the form writePmma() bakes, using no Vulkan
 * device.
 *
 * Every scene node that references a mesh contributes one part per primitive,
 * carrying that node's world transform, so the result needs no further
 * hierarchy walking. Geometry, materials and decoded images are shared: two
 * nodes on the same glTF mesh reference one AssetMesh, and one image sampled
 * by two materials occupies one texture slot per colour space it is read in.
 *
 * Mip chains are built here, so nothing downstream generates them.
 *
 * @param path a .gltf or .glb.
 * @param settings meshlet and CLOD generation.
 * @param out filled on success, left partially filled on failure.
 * @return false after logging on a parse error or an unsupported primitive.
 */
export [[nodiscard]] bool importGltf(const std::filesystem::path& path,
                                     const GltfImportSettings& settings,
                                     ImportedAsset& out);
