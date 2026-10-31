import GPUTypes;

[[vk::push_constant]] ConstantBuffer<ScatterInstancesPush> pc;

// The C++ driver dispatches ceil(updateCount / 64) workgroups.
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    const uint updateIndex = threadId.x;
    if (updateIndex >= pc.updateCount) return;  // tail of the last workgroup

    const GPUMeshInstanceUpdate update = pc.updates[updateIndex];
    pc.instances[update.index] = update.instance;
}
