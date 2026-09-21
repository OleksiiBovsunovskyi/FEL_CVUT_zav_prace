import gpu_types;
import ShereVisibilityTest;

struct GPUMeshTaskCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

/// Mirror of BuildDrawCommandsPush in Core/GPUTypes.ixx.
struct BuildDrawCommandsPush {
    GPUCameraData*      camera;     // 0
    GPUMeshInstance*    instances;  // 8
    GPUDrawData*        drawData;   // 16
    GPUMeshTaskCommand* commands;   // 24
    Atomic<uint>*   commandCount;   // 32

    uint instanceCount;             // 40
};                                  // 44

[[vk::push_constant]] ConstantBuffer<BuildDrawCommandsPush> pc;

// The C++ driver dispatches ceil(instanceCount / 64) workgroups.
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    const uint instanceIndex = threadId.x;
    if (instanceIndex >= pc.instanceCount) return;  // tail of the last workgroup

    const GPUMeshInstance instance = pc.instances[instanceIndex];
    const GPUMeshHeader mesh   = instance.mesh[0];

    if (mesh.meshletCount == 0) return;

    if (!sphereVisible(pc.camera->viewProj, instance.transform, mesh.boundingSphere)) return;

    // Claims this draw's slot and counts it in one operation.
    const uint slot = pc.commandCount->add(1);

    pc.commands[slot].groupCountX = mesh.meshletCount;
    pc.commands[slot].groupCountY = 1;
    pc.commands[slot].groupCountZ = 1;

    pc.drawData[slot].instanceIndex = instanceIndex;
    pc.drawData[slot].mesh        = instance.mesh;
}
