module;

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>

export module GPUDraw;
import GPUMesh;
/**
 * Indirect draw records written by GPU culling, and the counters read back from it.
 */

/// Written per surviving draw, at the same index as its mesh-task command.
export struct alignas(16) GPUDrawData {
    uint32_t        instanceIndex = 0;
    uint32_t        _padding    = 0;
    MeshDataPointer mesh{};
};

static_assert(sizeof(GPUDrawData) == 16);

/**
 * Draw commands one recording built.
 */
export struct GPUDrawCommandCount {
    uint32_t commandCount = 0;
};

static_assert(sizeof(GPUDrawCommandCount) == 4);
/// vkCmdDrawMeshTasksIndirectCountEXT reads the count at the start of the range.
static_assert(offsetof(GPUDrawCommandCount, commandCount) == 0);

/**
 * Counters of one recording, read back for metrics only.
 */
export struct GPUDrawCounters {
    /// Instances the recording submits; written on the CPU.
    uint32_t instances          = 0;
    uint32_t instancesDrawn     = 0;
    uint32_t meshletsDrawn      = 0;
    uint32_t meshletsCulled     = 0;
    uint32_t trianglesSubmitted = 0;
    uint32_t trianglesDrawn     = 0;
};

static_assert(sizeof(GPUDrawCounters) == 24);

/// Written by build_draw_commands.comp, consumed by vkCmdDrawMeshTasksIndirect*.
export using GPUMeshTaskCommand = VkDrawMeshTasksIndirectCommandEXT;

static_assert(sizeof(GPUMeshTaskCommand) == 12);
