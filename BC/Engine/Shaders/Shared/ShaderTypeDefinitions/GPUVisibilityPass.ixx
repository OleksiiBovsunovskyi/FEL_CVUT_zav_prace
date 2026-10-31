module;
#include <vulkan/vulkan_core.h>

export module GPUVisibilityPass;

import GPUMesh;
import GPUCoreTypes;
import GPUFrame;
import GPUDraw;
import GPUInstance;
//Invalid texture index for the depth pyramid
//(We want to do oc culling for visibility pass)
import GPUMaterial;

export struct GPUVisibilityPassPush
{
    GpuPtr<GPUFrameData> frame;
    //Geometry we are going to draw
    GpuPtr<GPUDrawData> drawData;
    //Instances we are going to draw
    GpuPtr<GPUMeshInstance>   instances;
    uint32_t depthPyramidSlot = INVALID_TEXTURE_INDEX;
};
