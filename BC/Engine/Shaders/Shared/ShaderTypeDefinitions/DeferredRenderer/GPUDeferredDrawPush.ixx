module;

export module GPUDeferredDrawPush;

import GPUCoreTypes;
import GPUMaterial;
import GPUFrame;
import GPUDraw;
import Instance;
#include <cstdint>


export struct GPUDeferredDrawPush
{
    GpuPtr<GPUFrameData> frame;
    GpuPtr<GPUDrawData> drawData;
    GpuPtr<Instance>   instances;
    GpuPtr<GPUMaterial> materials;

    /// Sampled slot of the full depth pyramid chain.
    uint32_t depthPyramidSlot = INVALID_TEXTURE_INDEX;
    uint32_t visibilityBufferSlot = INVALID_TEXTURE_INDEX;
};
