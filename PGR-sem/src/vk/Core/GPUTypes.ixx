module;
#include <vulkan/vulkan.hpp>

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
 * A plain vk::DeviceAddress makes every buffer the same type, so swapping two
 * push constant fields compiles and reads garbage on the GPU. Naming the
 * pointee makes that a compile error. Obtained from a DeviceSpan or MappedSpan
 * handed out by BufferManager.
 */
export template <typename T>
struct GpuPtr {
    vk::DeviceAddress address = 0;
};

static_assert(sizeof(GpuPtr<float>) == sizeof(vk::DeviceAddress));

/**
 * Device address of a T[count]. The shader-facing form of a range; a bare
 * address plus a loose length is what it replaces.
 */
export template <typename T>
struct GpuSpan {
    GpuPtr<T> data{};
    uint32_t  count = 0;

    [[nodiscard]] explicit operator bool() const {
        return data.address != 0 && count != 0;
    }
};

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

/// Record index into the Materials mega-buffer.
export struct GPUMaterialIndex {
    uint32_t value = 0;
};

static_assert(sizeof(GPUMaterialIndex) == 4);

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
    uint32_t _padding     = 0;
    glm::vec4 boundingSphere{};
};

static_assert(sizeof(GPUMeshHeader) == 64);

/// Address of a mesh's header. What a GPUMeshInstance and a GPUDrawData carry.
export using MeshDataPointer = GpuPtr<GPUMeshHeader>;

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
 * One drawable instance. transform is model-to-world; MultiMesh parts are
 * flattened on the CPU, so one part is one of these.
 *
 * The trailing 8 bytes are alignment padding held for a per-instance material
 * override, which fits without growing the record.
 */
export struct alignas(16) GPUMeshInstance {
    glm::mat4      transform{1.0f};
    MeshDataPointer mesh{};
    glm::uvec2     _padding{};
};

static_assert(sizeof(GPUMeshInstance) == 80);

/// Written per surviving draw, at the same index as its mesh-task command.
export struct alignas(16) GPUDrawData {
    uint32_t        instanceIndex = 0;
    uint32_t        _padding    = 0;
    MeshDataPointer mesh{};
};

static_assert(sizeof(GPUDrawData) == 16);

/// Written by build_draw_commands.comp, consumed by vkCmdDrawMeshTasksIndirect*.
export using GPUMeshTaskCommand = vk::DrawMeshTasksIndirectCommandEXT;

static_assert(sizeof(GPUMeshTaskCommand) == 12);

/**
 * Arguments to build_draw_commands.comp.
 */
export struct alignas(16) BuildDrawCommandsPush {
    glm::mat4                  viewProj{1.0f};
    GpuPtr<GPUMeshInstance>    instances;      //transform + the mesh it draws
    GpuPtr<GPUDrawData>        drawData;     //Output. Object index and mesh, read back by the mesh shader
    GpuPtr<GPUMeshTaskCommand> commands;     //Output. Actual draw command, built here
    GpuPtr<uint32_t>           commandCount; //Output. How many actual draw commands have been built
    uint32_t                   instanceCount = 0;  //Amount of instances to draw
    uint32_t                   _padding    = 0;
};

static_assert(sizeof(BuildDrawCommandsPush) == 112);

/**
 * Arguments to mesh.slang. Geometry is reached through the mesh header a
 * GPUDrawData points at, so only the two per-frame arrays and the material
 * table need a base here.
 */
export struct GPUMeshDrawPush {
    glm::mat4           viewProj{1.0f};
    GpuPtr<GPUDrawData> drawData;
    GpuPtr<GPUMeshInstance>   instances;
    GpuPtr<GPUMaterial> materials;
};

static_assert(sizeof(GPUMeshDrawPush) == 88);
