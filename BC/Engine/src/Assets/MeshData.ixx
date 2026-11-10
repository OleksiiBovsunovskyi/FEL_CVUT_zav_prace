module;

#include <span>

#include <glm/glm.hpp>

export module MeshData;

export import GPUTypes;

/// Local-space bounding sphere of an entire mesh.
export struct MeshBounds {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};


export struct MeshUploadData {
    /// Indexed by Meshlet::firstMeshletVertex.
    std::span<const Vertex> vertices;
    std::span<const Meshlet> meshlets;
    /// Indexed by Meshlet::firstMeshletTriangle.
    std::span<const Triangle> triangles;
    /// Grid the vertices and meshlets are packed on.
    PositionGrid grid{};
    /// Local-space bounds of the complete mesh.
    MeshBounds bounds{};
};
