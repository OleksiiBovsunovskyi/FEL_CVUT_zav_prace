module;
#include <cstdint>
#include <span>
#include <vector>

export module ClusterLODGenerator;

export import Mesh;

/**
 * Output limits declared by shaders/mesh.mesh. generateClusterLOD rejects
 * settings above these, so a meshlet the mesh shader cannot emit is never built.
 *
 * The shader repeats the numbers as literals; nothing links the two. Passing them
 * through as shader macros would close that.
 */
export constexpr uint32_t MESHLET_MAX_VERTICES  = 64;
export constexpr uint32_t MESHLET_MAX_TRIANGLES = 64;

export struct ClusterLODSettings {
    /// False generates one terminal group; true requests a deeper CLOD DAG.
    bool enabled = false;

    uint32_t maxVerticesPerCluster  = 64;
    uint32_t maxTrianglesPerCluster = 64;
    float simplifyRatio             = 0.5f;
    float simplifyThreshold         = 0.85f;
};

export struct GeneratedClusterLOD {
    std::vector<GPUMeshlet> meshlets;
    std::vector<uint32_t> meshletVertexIndices;
    std::vector<uint32_t> meshletTriangles;
    std::vector<GPUCluster> clusters;
    std::vector<GPUClusterGroup> groups;

    [[nodiscard]] bool empty() const { return clusters.empty(); }

    [[nodiscard]] MeshUploadData uploadData(
        std::span<const GPUVertex> vertices, MeshBounds bounds) const {
        return MeshUploadData{
            vertices,
            meshlets,
            meshletVertexIndices,
            meshletTriangles,
            clusters,
            groups,
            bounds,
        };
    }
};

/**
 * Always builds meshlets. With CLOD disabled, output contains one terminal
 * group at depth 0. Otherwise it builds all hierarchy levels at load time.
 *
 * Input indices reference GPUVertex entries. Generated triangle indices are
 * packed as i0 | i1<<8 | i2<<16, ready for the mesh shader.
 */
export [[nodiscard]] bool generateClusterLOD(
    std::span<const GPUVertex> vertices,
    std::span<const uint32_t> indices,
    const ClusterLODSettings& settings,
    GeneratedClusterLOD& output);
