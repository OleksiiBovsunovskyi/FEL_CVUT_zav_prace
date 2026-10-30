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
    /// Vertex data referenced by meshletVertexIndices.
    std::span<const GPUVertex> vertices;
    /// Every generated meshlet across all hierarchy levels.
    std::span<const GPUMeshlet> meshlets;
    /// Flattened global vertex indices used by meshlets.
    std::span<const uint32_t> meshletVertexIndices;
    /// One triangle per uint32: i0 | (i1 << 8) | (i2 << 16).
    std::span<const uint32_t> meshletTriangles;
    /// One DAG record per meshlet.
    std::span<const GPUCluster> clusters;
    /// CLOD groups;
    std::span<const GPUClusterGroup> clusterGroups;
    /// Local-space bounds of the complete mesh.
    MeshBounds bounds{};
};
