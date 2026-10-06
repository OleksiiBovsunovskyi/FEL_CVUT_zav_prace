import gpu_types;

/// Mirror of DepthPyramidReducePush in Core/GPUTypes.ixx.
struct DepthPyramidReducePush {
    GPUHiZTexel* source;           // 0
    GPUHiZTexel* destination;      // 8

    uint sourceWidth;              // 16
    uint sourceHeight;             // 20
};                                 // 24

[[vk::push_constant]] ConstantBuffer<DepthPyramidReducePush> pc;

// Downsample depth texture
// Result = Min of 2x2 neighbours
[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
     
    const uint destinationWidth  = max(pc.sourceWidth  / 2, 1u);
    const uint destinationHeight = max(pc.sourceHeight / 2, 1u);

    const uint index = threadId.x;
    
    if (index >= destinationWidth * destinationHeight) return;

    const uint x = index % destinationWidth;
    const uint y = index / destinationWidth;

    
    const uint x0 = 2 * x;
    const uint y0 = 2 * y;
     
     //Last texel in the odd sized depth 
    const uint x1 = (x == destinationWidth  - 1) ? pc.sourceWidth  - 1 : x0 + 1;
    const uint y1 = (y == destinationHeight - 1) ? pc.sourceHeight - 1 : y0 + 1;

    float depth = 3.4e38;
    //Min depth of for 
    for (uint row = y0; row <= y1; ++row)
        for (uint column = x0; column <= x1; ++column)
            depth = min(depth, pc.source[row * pc.sourceWidth + column].depth);

    pc.destination[index].depth = depth;
}
