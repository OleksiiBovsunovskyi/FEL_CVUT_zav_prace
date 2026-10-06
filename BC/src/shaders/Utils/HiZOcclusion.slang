module HiZOcclusion;

import gpu_types;

// Reverse-Z: 0 is far and 1 is near, so a pyramid texel keeps the minimum depth.

// Texels before `level` in the packed pyramid. Level i is max(size >> i, 1) per axis.
uint levelOffset(uint2 size, uint level) {
    uint offset = 0;
    for (uint i = 0; i < level; ++i)
        offset += max(size.x >> i, 1u) * max(size.y >> i, 1u);
    return offset;
}

/// Depth-pyramid occlusion test for a world-space bounding sphere.
/// Conservative: a sphere that reaches or crosses the camera plane counts as visible.
///
/// @param viewProj world -> clip.
/// @param sphere xyz = centre in world space, w = radius.
/// @param hiZ levels packed one after another, level 0 first.
/// @param size level 0 size in texels.
/// @param levels levels in the pyramid; 0 disables the test.
/// @return true when every texel under the sphere's screen rectangle is nearer than the sphere.
public bool occludedByHiZ(float4x4 viewProj, float4 sphere, GPUHiZTexel* hiz,
                          uint2 size, uint levels) {
    if (levels == 0) return false;

    // The eight corners of the sphere's world-space box bound its projection.
    float2 ndcMin  = float2(1e30, 1e30);
    float2 ndcMax  = float2(-1e30, -1e30);
    float  nearest = 0.0;
    for (uint i = 0; i < 8; ++i) {
        const float3 offset = float3((i & 1) != 0 ? 1.0 : -1.0,
                                     (i & 2) != 0 ? 1.0 : -1.0,
                                     (i & 4) != 0 ? 1.0 : -1.0);
        const float4 clip = mul(viewProj, float4(sphere.xyz + sphere.w * offset, 1.0));
        if (clip.w <= 0.0) return false;

        const float3 ndc = clip.xyz / clip.w;
        ndcMin  = min(ndcMin, ndc.xy);
        ndcMax  = max(ndcMax, ndc.xy);
        nearest = max(nearest, ndc.z);
    }

    const float2 pixelMin = saturate(ndcMin * 0.5 + 0.5) * float2(size);
    const float2 pixelMax = saturate(ndcMax * 0.5 + 0.5) * float2(size);
    const float2 extent   = pixelMax - pixelMin;

    // The level whose texel is at least as large as the rectangle, so four corner taps cover it.
    const uint level = min(uint(ceil(log2(max(max(extent.x, extent.y), 1.0)))), levels - 1);

    const uint2 levelSize = uint2(max(size.x >> level, 1u), max(size.y >> level, 1u));
    const float texelSize = float(1u << level);
    const int2  last      = int2(levelSize) - 1;
    const int2  lo        = clamp(int2(floor(pixelMin / texelSize)), int2(0, 0), last);
    const int2  hi        = clamp(int2(floor(pixelMax / texelSize)), int2(0, 0), last);

    GPUHiZTexel* texels = hiz + levelOffset(size, level);
    const float d00 = texels[lo.y * levelSize.x + lo.x].depth;
    const float d10 = texels[lo.y * levelSize.x + hi.x].depth;
    const float d01 = texels[hi.y * levelSize.x + lo.x].depth;
    const float d11 = texels[hi.y * levelSize.x + hi.x].depth;

    return nearest < min(min(d00, d10), min(d01, d11));
}
