module;
#include <cstdint>
#include <limits>

export module GPUTypes;

/**
 * Every shader-facing record. VK_Buffers takes each static buffer's stride from
 * sizeof() of the record it holds.
 *
 * Layouts are std430/scalar compatible; the static_asserts are the contract with
 * shaders/gpu_types.glsl.
 */

export constexpr uint32_t INVALID_GPU_MESH_INDEX =
    std::numeric_limits<uint32_t>::max();

export constexpr uint32_t INVALID_TEXTURE_INDEX =
    std::numeric_limits<uint32_t>::max();

/**
 * Interleaved vertex. The explicit 16-byte fields keep the C++ and GLSL layouts
 * identical. position.w and normal.w are reserved; tangent.w is handedness.
 */
export struct alignas(16) GPUVertex {
    float position[4] = {}; // byte offset 0
    float normal[4]   = {}; // byte offset 16
    float tangent[4]  = {}; // byte offset 32
    float texCoord[2] = {}; // byte offset 48
    float _padding[2] = {}; // byte offset 56
};

static_assert(sizeof(GPUVertex) == 64);

/**
 * One meshlet, processed by one mesh-shader workgroup.
 *
 * vertexOffset and triangleOffset are relative to the owning GPUMesh's
 * meshletVertexIndexOffset and meshletTriangleOffset. Triangle indices are
 * meshlet-local. Bounds and normal cone are in mesh space.
 */
export struct alignas(16) GPUMeshlet {
    uint32_t vertexOffset   = 0;
    uint32_t triangleOffset = 0;
    uint32_t vertexCount    = 0;
    uint32_t triangleCount  = 0;

    float boundingSphere[4] = {};
    /// xyz = unit cone axis, w = cosine cutoff used for backface cone culling.
    float normalCone[4] = {};
};

static_assert(sizeof(GPUMeshlet) == 48);

/**
 * Connects one meshlet to the flat CLOD DAG. All three indices are relative to
 * the owning GPUMesh.
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
 * firstCluster is relative to GPUMesh::clusterOffset. error is geometric, in
 * mesh-space units, FLT_MAX for terminal groups; the renderer projects it to
 * pixels to choose between this group and a refined one.
 */
export struct alignas(16) GPUClusterGroup {
    uint32_t firstCluster = 0;
    uint32_t clusterCount = 0;
    uint32_t depth        = 0;
    uint32_t _padding0    = 0;
    float boundingSphere[4] = {};
    float error             = 0.0f;
    float _padding1[3]      = {};
};

static_assert(sizeof(GPUClusterGroup) == 48);

/**
 * Record for one single-material mesh. Every offset is an element index into its
 * mega-buffer, taken from BufferSlice::elementIndex.
 */
export struct alignas(16) GPUMesh {
    uint32_t vertexOffset              = 0;
    uint32_t vertexCount               = 0;
    uint32_t meshletOffset             = 0;
    uint32_t meshletCount              = 0;
    uint32_t meshletVertexIndexOffset  = 0;
    uint32_t meshletTriangleOffset     = 0;
    uint32_t materialIndex             = 0;
    uint32_t _padding                  = 0;
    uint32_t clusterOffset             = 0;
    uint32_t clusterCount              = 0;
    uint32_t clusterGroupOffset        = 0;
    uint32_t clusterGroupCount         = 0;
    float boundingSphere[4]            = {};
};

static_assert(sizeof(GPUMesh) == 64);

export enum MaterialFlags : uint32_t {
    MATERIAL_EMISSIVE    = 1u << 0,
    MATERIAL_ALPHA_BLEND = 1u << 1,
    MATERIAL_ALPHA_MASK  = 1u << 2,
};

/// Material record; five 16-byte blocks.
export struct alignas(16) GPUMaterial {
    float albedo[4]   = {1.0f, 1.0f, 1.0f, 1.0f};
    float emissive[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    /* x=specular intensity, y=shininess, z=metallic, w=roughness */
    float surface[4] = {0.5f, 32.0f, 0.0f, 0.1f};

    /* Albedo, normal, ORM and emissive bindless descriptor indices. */
    uint32_t textures[4] = {
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
    };

    float    alphaThreshold = 0.0f;
    uint32_t flags          = 0;
    uint32_t _padding[2]    = {};
};

static_assert(sizeof(GPUMaterial) == 80);
