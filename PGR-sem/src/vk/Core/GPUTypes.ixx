module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

export module GPUTypes;

/**
 * Every shader-facing record. VK_Buffers takes each static buffer's stride from
 * sizeof() of the record it holds.
 *
 * Layouts are std430/scalar compatible; the static_asserts are the contract with
 * shaders/gpu_types.glsl.
 */

/**
 * Device address of an array of T, and the C++ spelling of a `T*` in a shader.
 *
 * A plain VkDeviceAddress makes every buffer the same type, so swapping two
 * push constant fields compiles and reads garbage on the GPU. Naming the
 * pointee makes that a compile error. Obtained from
 * BufferSlice/MegaBufferView::deviceAddressAs<T>().
 */
export template <typename T>
struct GpuPtr {
    VkDeviceAddress address = 0;
};

static_assert(sizeof(GpuPtr<float>) == sizeof(VkDeviceAddress));

export constexpr uint32_t INVALID_GPU_MESH_INDEX =
    std::numeric_limits<uint32_t>::max();

export constexpr uint32_t INVALID_TEXTURE_INDEX =
    std::numeric_limits<uint32_t>::max();

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

    glm::vec4 boundingSphere{};
    /// xyz = unit cone axis, w = cosine cutoff used for backface cone culling.
    glm::vec4 normalCone{};
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
    glm::vec4 boundingSphere{};
    float     error          = 0.0f;
    glm::vec3 _padding1{};
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
    glm::vec4 boundingSphere{};
};

static_assert(sizeof(GPUMesh) == 64);

export enum MaterialFlags : uint32_t {
    MATERIAL_EMISSIVE    = 1u << 0,
    MATERIAL_ALPHA_BLEND = 1u << 1,
    MATERIAL_ALPHA_MASK  = 1u << 2,
};

/// Material record; five 16-byte blocks.
export struct alignas(16) GPUMaterial {
    glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 emissive{0.0f, 0.0f, 0.0f, 1.0f};

    /* x=specular intensity, y=shininess, z=metallic, w=roughness */
    glm::vec4 surface{0.5f, 32.0f, 0.0f, 0.1f};

    /* Albedo, normal, ORM and emissive bindless descriptor indices. */
    glm::uvec4 textures{
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
    };

    float      alphaThreshold = 0.0f;
    uint32_t   flags          = 0;
    glm::uvec2 _padding{};
};

static_assert(sizeof(GPUMaterial) == 80);

/**
 * One drawable instance. transform is model-to-world.
 */
export struct alignas(16) GPUObject {
    glm::mat4  transform{1.0f};
    uint32_t   meshIndex = INVALID_GPU_MESH_INDEX;
    glm::uvec3 _padding{};
};

static_assert(sizeof(GPUObject) == 80);

/// Written per surviving draw, at the same index as its mesh-task command.
export struct alignas(16) GPUDrawData {
    uint32_t   objectIndex = 0;
    uint32_t   meshIndex   = 0;
    glm::uvec2 _padding{};
};

static_assert(sizeof(GPUDrawData) == 16);

/// Written by build_draw_commands.comp, consumed by vkCmdDrawMeshTasksIndirect*.
export using GPUMeshTaskCommand = VkDrawMeshTasksIndirectCommandEXT;

static_assert(sizeof(GPUMeshTaskCommand) == 12);

/**
 * Arguments to build_draw_commands.comp.
 */
export struct alignas(16) BuildDrawCommandsPush {
    glm::mat4                 viewProj{1.0f};
    GpuPtr<GPUObject>          objects;      //transforms
    GpuPtr<GPUMesh>            meshes;       //offsets in the mesh megabuffer to find mesh data
    GpuPtr<GPUDrawData>        drawData;     //Output. Object and mesh indices, used to retrieve actual mesh data and its material
    GpuPtr<GPUMeshTaskCommand> commands;     //Output. Actual draw command, built here
    GpuPtr<uint32_t>           commandCount; //Output. How many actual draw commands have been built
    uint32_t                   objectCount = 0;  //Amount of objects to draw
    uint32_t                   _padding    = 0;
};

static_assert(sizeof(BuildDrawCommandsPush) == 112);

/**
 * Arguments to mesh.mesh. The mega-buffer bases are the ones GPUMesh's offsets
 * index into; build_draw_commands.comp needs none of them.
 *
 * 120 of the 128 guaranteed push constant bytes.
 */
export struct MeshDrawPush {
    glm::mat4             viewProj{1.0f};
    GpuPtr<GPUDrawData>   drawData;
    GpuPtr<GPUObject>     objects;
    GpuPtr<GPUMesh>       meshes;
    GpuPtr<GPUVertex>     vertices;
    GpuPtr<GPUMeshlet>    meshlets;
    GpuPtr<uint32_t>      meshletVertexIndices;
    GpuPtr<uint32_t>      meshletTriangles;
};

static_assert(sizeof(MeshDrawPush) == 120);
