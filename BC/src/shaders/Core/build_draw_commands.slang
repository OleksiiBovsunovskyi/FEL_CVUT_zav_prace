import gpu_types;
import AtomicAdd;
import SphereVisibilityTest;
import HiZOcclusion;

struct GPUMeshTaskCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

/// Mirror of BuildDrawCommandsPush in Core/GPUTypes.ixx.
struct BuildDrawCommandsPush {
    GPUFrameData*        frame;        // 0
    GPUMeshInstance*     instances;    // 8
    GPUDrawData*         drawData;     // 16
    GPUMeshTaskCommand*  commands;     // 24
    GPUDrawCommandCount* commandCount; // 32
    GPUInstanceVisibility* previousVisibility; // 40
    GPUInstanceVisibility* visibility;         // 48
    GPUHiZTexel*         hiZ;          // 56

    uint instanceCount;                // 64
    uint previousVisibilityCount;      // 68
    uint pass;                         // 72
    uint hiZWidth;                     // 76
    uint hiZHeight;                    // 80
    uint hiZLevels;                    // 84
};                                     // 88

[[vk::push_constant]] ConstantBuffer<BuildDrawCommandsPush> pc;

// The C++ driver dispatches ceil(instanceCount / 64) workgroups.
// Pass 0 draws the frustum-visible instances the previous recording drew.
// Pass 1 dispatches every frustum-visible instance the depth pyramid does not hide,
// and records which instances are visible for the next recording's pass 0.
// mesh.slang decides per meshlet which of those dispatched meshlets each pass draws.
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    const uint instanceIndex = threadId.x;
    if (instanceIndex >= pc.instanceCount) return;  // tail of the last workgroup

    const GPUMeshInstance instance = pc.instances[instanceIndex];
    const GPUMeshHeader mesh   = instance.mesh[0];

    if (mesh.meshletCount == 0) return;

    const bool secondPass = pc.pass == BUILD_PASS_SECOND;

    GPUDrawCounters* counters = pc.frame->drawCounters;
    if (!secondPass) atomicAdd(&counters->trianglesSubmitted, mesh.triangleCount);
    
    // Frustum test 
    const float4 sphere    = worldSphere(instance.transform, mesh.boundingSphere);
    const bool   inFrustum = sphereVisibleWorld(pc.frame->camera.viewProj, sphere);

    // Entries past previousVisibilityCount are unallocated, so the read sits behind its own branch.
    bool drawnInFirstPass = false;
    if (inFrustum && instanceIndex < pc.previousVisibilityCount)
        drawnInFirstPass = pc.previousVisibility[instanceIndex].visible != 0;

    // With occlusion disabled the first pass is the only pass and draws every frustum-visible instance.
    const bool occlusionOff = pc.frame->debugDisableOcclusion != 0;

    bool draw = occlusionOff ? inFrustum : drawnInFirstPass;
    if (secondPass) {
        bool occluded = false;
        if (inFrustum)
            occluded = occludedByHiZ(pc.frame->camera.viewProj, sphere, pc.hiZ,
                                     uint2(pc.hiZWidth, pc.hiZHeight), pc.hiZLevels);
        if (occluded && !drawnInFirstPass) atomicAdd(&counters->instancesOccluded, 1);

        const bool visible = inFrustum && !occluded;
        pc.visibility[instanceIndex].visible = visible ? 1 : 0;
        draw = visible;
    }
    if (!draw) return;

    // Claims this draw's slot and counts it in one operation.
    const uint slot = atomicAdd(&pc.commandCount->commandCount, 1);
    if (!secondPass || !drawnInFirstPass) atomicAdd(&counters->instancesDrawn, 1);

    pc.commands[slot].groupCountX = mesh.meshletCount;
    pc.commands[slot].groupCountY = 1;
    pc.commands[slot].groupCountZ = 1;

    pc.drawData[slot].instanceIndex = instanceIndex;
    pc.drawData[slot].drawnInFirstPass = drawnInFirstPass ? 1 : 0;
    pc.drawData[slot].mesh        = instance.mesh;
}
