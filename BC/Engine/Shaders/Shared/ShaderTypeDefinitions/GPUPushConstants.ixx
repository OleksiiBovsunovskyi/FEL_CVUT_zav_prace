module;

#include <cstdint>

export module GPUPushConstants;
import GPUCoreTypes;
import GPUMaterial;
import GPUInstance;
import GPUDraw;
import GPUFrame;
import GPUDepthPyramid;
/**
 * Push constants of the engine's shaders, one struct per shader.
 */

/**
 * Arguments to build_draw_commands.comp.
 */
export struct alignas(16) BuildDrawCommandsPush {
    GpuPtr<GPUFrameData>       frame;        //viewProj the culling test uses, draw counters
    GpuPtr<GPUMeshInstance>    instances;      //transform + the mesh it draws
    GpuPtr<GPUDrawData>        drawData;     //Output. Object index and mesh, read back by the mesh shader
    GpuPtr<GPUMeshTaskCommand> commands;     //Output. Actual draw command, built here
    GpuPtr<GPUDrawCommandCount> commandCount; //Output. Hands out each command's slot
    uint32_t                   instanceCount = 0;  //Amount of instances to draw
    uint32_t                   depthPyramidSlot = INVALID_TEXTURE_INDEX; //Sampled slot of the full pyramid chain
};

static_assert(sizeof(BuildDrawCommandsPush) == 48);

/**
 * Arguments to scatter_instances.slang.
 */
export struct ScatterInstancesPush {
    GpuPtr<GPUMeshInstanceUpdate> updates;      //Instance writes to apply
    GpuPtr<GPUMeshInstance>       instances;    //Output. The device instance array
    uint32_t                      updateCount = 0;
    uint32_t                      _padding    = 0;
};

static_assert(sizeof(ScatterInstancesPush) == 24);

/**
 * Arguments to mesh.slang.
 * Geometry is reached through the mesh header a GPUDrawData points at.
 */
export struct GPUMeshDrawPush {
    GpuPtr<GPUFrameData> frame;
    GpuPtr<GPUDrawData> drawData;
    GpuPtr<GPUMeshInstance>   instances;
    GpuPtr<GPUMaterial> materials;
    /// Sampled slot of the full depth pyramid chain.
    uint32_t depthPyramidSlot = INVALID_TEXTURE_INDEX;
    uint32_t _padding = 0;
};

static_assert(sizeof(GPUMeshDrawPush) == 40);


export struct GPUDepthPyramidPush {
    GpuPtr<GPUDepthPyramidCounter> counter;
    /// Sampled slot of the depth image the pyramid is built from.
    uint32_t depthSlot = INVALID_TEXTURE_INDEX;
    /// Storage slot of pyramid mip 0; mip n is at pyramidMip0Slot + n.
    uint32_t pyramidMip0Slot = INVALID_TEXTURE_INDEX;
    uint32_t mipCount = 0;
    uint32_t _padding = 0;
};
static_assert(sizeof(GPUDepthPyramidPush) == 24);

export struct GPUHIZOCPush {
    GpuPtr<GPUFrameData> frame;
    GpuPtr<GPUMeshInstance> instances;
};
static_assert(sizeof(GPUHIZOCPush) == 16);
