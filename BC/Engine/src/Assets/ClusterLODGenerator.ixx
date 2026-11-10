module;
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module ClusterLODGenerator;

export import MeshData;

/**
 * Mesh-shader output limits. generateClusterLOD rejects settings above these,
 * so a meshlet the mesh shader cannot emit is never built.
 */
export constexpr uint32_t MESHLET_MAX_VERTICES  = 64;
export constexpr uint32_t MESHLET_MAX_TRIANGLES = 64;

export struct ClusterLODSettings {
    uint32_t maxVerticesPerCluster  = 64;
    uint32_t maxTrianglesPerCluster = 64;
};

/// Unpacked vertex of an imported primitive, in mesh space.
export struct ImportedVertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec4 tangent{}; // xyz = tangent, w = bitangent handedness
    glm::vec2 texCoord{};
};

export struct GeneratedClusterLOD {
    /// Each meshlet's vertices are contiguous, in meshlet order.
    std::vector<Vertex>   vertices;
    std::vector<Meshlet>  meshlets;
    /// Each meshlet's triangles are contiguous, in meshlet order.
    std::vector<Triangle> triangles;
    /// Grid the vertices and meshlets are packed on.
    PositionGrid grid{};

    [[nodiscard]] bool empty() const { return meshlets.empty(); }

    [[nodiscard]] MeshUploadData uploadData(MeshBounds bounds) const {
        return MeshUploadData{vertices, meshlets, triangles, grid, bounds};
    }
};

/**
 * Builds meshlets and packs their vertices and triangles.
 * A vertex shared by several meshlets is written once per meshlet.
 *
 * @param vertices vertices of one primitive
 * @param indices triangle list indexing vertices
 * @param settings meshlet size limits
 * @param output replaced with the packed geometry
 * @return false after logging on bad input or a meshlet that fails validation.
 */
export [[nodiscard]] bool generateClusterLOD(
    std::span<const ImportedVertex> vertices,
    std::span<const uint32_t> indices,
    const ClusterLODSettings& settings,
    GeneratedClusterLOD& output);
