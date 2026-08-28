import gpu_types;
import ShereVisibilityTest;

struct GPUMeshTaskCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

/// Mirror of BuildDrawCommandsPush in Core/GPUTypes.ixx.
struct BuildDrawCommandsPush {
    float4x4 viewProj;              // 0

    GPUObject*          objects;    // 64
    GPUMesh*            meshes;     // 72
    GPUDrawData*        drawData;   // 80
    GPUMeshTaskCommand* commands;   // 88
    Atomic<uint>*   commandCount;   // 96

    uint objectCount;               // 104
};                                  // 108

[[vk::push_constant]] ConstantBuffer<BuildDrawCommandsPush> pc;

// The C++ driver dispatches ceil(objectCount / 64) workgroups.
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    const uint objectIndex = threadId.x;
    if (objectIndex >= pc.objectCount) return;  // tail of the last workgroup

    const GPUObject object = pc.objects[objectIndex];
    const GPUMesh   mesh   = pc.meshes[object.meshIndex];

    if (mesh.meshletCount == 0) return;

    if (!sphereVisible(pc.viewProj, object.transform, mesh.boundingSphere)) return;

    // Claims this draw's slot and counts it in one operation.
    const uint slot = pc.commandCount->add(1);

    pc.commands[slot].groupCountX = mesh.meshletCount;
    pc.commands[slot].groupCountY = 1;
    pc.commands[slot].groupCountZ = 1;

    pc.drawData[slot].objectIndex = objectIndex;
    pc.drawData[slot].meshIndex   = object.meshIndex;
}
