module;

#include <cstdint>

#include <glm/glm.hpp>

export module GPUMesh;
import GPUCoreTypes;
import GPUMaterial;
/**
 * Mesh geometry records: vertices, meshlets, CLOD clusters and the mesh header.
 */

/**
 * Interleaved vertex. The explicit 16-byte fields keep the C++ and GLSL layouts
 * identical. position.w and normal.w are reserved; tangent.w is handedness.
 */
export struct alignas(16) GPUVertex {
    glm::vec4 position{}; // byte offset 0
    glm::vec4 normal{};   // byte offset 16
    glm::vec4 tangent{};  // byte offset 32
    glm::vec2 texCoord{}; // byte offset 48
    glm::vec2 _padding{}; // byte offset 56
};

static_assert(sizeof(GPUVertex) == 64);

/// Meshlet-local vertex slot to an index of a GPUVertex in the same mesh.
export struct GPUMeshletVertexIndex {
    uint32_t value = 0;
};

static_assert(sizeof(GPUMeshletVertexIndex) == 4);

/// One triangle, meshlet-local: i0 | (i1 << 8) | (i2 << 16).
export struct GPUMeshletTriangle {
    uint32_t packed = 0;
};

static_assert(sizeof(GPUMeshletTriangle) == 4);

/**
 * One meshlet, processed by one mesh-shader workgroup.
 *
 * vertexOffset and triangleOffset index the owning mesh's meshletVertexIndices
 * and meshletTriangles. Triangle indices are meshlet-local. Bounds and normal
 * cone are in mesh space.
 */
export struct alignas(16) GPUMeshlet {
    uint32_t vertexOffset   = 0;
    uint32_t triangleOffset = 0;
    uint32_t vertexCount    = 0;
    uint32_t triangleCount  = 0;

    glm::vec4 boundingSphere{};
    /// xyz = unit cone axis, w = cosine cutoff used for backface cone culling.
    glm::vec4 normalCone{};
};

static_assert(sizeof(GPUMeshlet) == 48);

/**
 * Connects one meshlet to the flat CLOD DAG. All three indices are relative to
 * the owning mesh. Generated at load; nothing uploads or reads it yet.
 *
 * group is the group containing this cluster; refinedGroup is the more detailed
 * group that produced it, or -1 for original geometry.
 */
export struct alignas(16) GPUCluster {
    uint32_t meshletIndex = 0;
    int32_t  group        = -1;
    int32_t  refinedGroup = -1;
    uint32_t _padding     = 0;
};

static_assert(sizeof(GPUCluster) == 16);

/**
 * Clusters selected together by the CLOD error test.
 *
 * firstCluster is relative to the mesh's cluster array. error is geometric, in
 * mesh-space units, FLT_MAX for terminal groups; the renderer projects it to
 * pixels to choose between this group and a refined one.
 */
export struct alignas(16) GPUClusterGroup {
    uint32_t firstCluster = 0;
    uint32_t clusterCount = 0;
    uint32_t depth        = 0;
    uint32_t _padding0    = 0;
    glm::vec4 boundingSphere{};
    float     error          = 0.0f;
    glm::vec3 _padding1{};
};

static_assert(sizeof(GPUClusterGroup) == 48);

/**
 * First record in a mesh's MeshData blob, and the only part of it anything
 * outside points at. Each pointer addresses a section of that same blob.
 */
export struct alignas(16) GPUMeshHeader {
    GpuPtr<GPUVertex>             vertices{};
    GpuPtr<GPUMeshlet>            meshlets{};
    GpuPtr<GPUMeshletVertexIndex> meshletVertexIndices{};
    GpuPtr<GPUMeshletTriangle>    meshletTriangles{};
    uint32_t vertexCount  = 0;
    uint32_t meshletCount = 0;
    GPUMaterialIndex material{};
    /// Triangles of every meshlet.
    uint32_t triangleCount = 0;
    glm::vec4 boundingSphere{};
};

static_assert(sizeof(GPUMeshHeader) == 64);

/// Address of a mesh's header. What a GPUMeshInstance and a GPUDrawData carry.
export using MeshDataPointer = GpuPtr<GPUMeshHeader>;
