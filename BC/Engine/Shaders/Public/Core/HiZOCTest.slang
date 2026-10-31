module HiZOCTest;


/**
 * A lot of stuff can be extracted and reused in other shaders, the same goes for the build depth pyramid
*/

/*
 * Tests if given sphere is ocluded by HiZ built with hiZViewProj (reverse Z)
 * @param pyramid depth pyramid; 
 * @param hiZViewProj view proj pyramid built with.
 * @param sphere xyz = centre in world space, w = radius.
 * @return true when the sphere is ocluded.
*/
public bool isOcludedByHiZ(Sampler2D pyramid, float4x4 hiZViewProj, float4 sphere) {
    float2 uvMin   = 1.0f;
    float2 uvMax   = 0.0f;
    float  nearest = 0.0f;
    for (uint corner = 0; corner < 8; ++corner) {
        const float3 offset = float3((corner & 1) != 0 ? 1.0f : -1.0f,
                                     (corner & 2) != 0 ? 1.0f : -1.0f,
                                     (corner & 4) != 0 ? 1.0f : -1.0f);
        /**Clip projection of the cube*/
        const float4 clip = mul(hiZViewProj, float4(sphere.xyz + sphere.w * offset /*Cube corner*/, 1.0f));
        if (clip.w <= 0.0f) return false;
        const float3 ndc = clip.xyz / clip.w;
        //project to uv space
        const float2 uv  = ndc.xy * 0.5f + 0.5f;
        
        //What screen rect does this cube (sphere) cower
        uvMin   = min(uvMin, uv);
        uvMax   = max(uvMax, uv);
        //Nearest depth this cube covers
        nearest = max(nearest, ndc.z);
    }
    //reject off screen and clamp to screen.
    if (any(uvMax < 0.0f) || any(uvMin > 1.0f)) return false;
    uvMin = saturate(uvMin);
    uvMax = saturate(uvMax);

    uint2 size;
    uint  mipCount;
    pyramid.GetDimensions(0, size.x, size.y, mipCount);

    //Low and high tex at mip 0
    const uint2 lo   = min(uint2(uvMin * float2(size)), size - 1);
    const uint2 hi   = min(uint2(uvMax * float2(size)), size - 1);
    //Size of the projceted rect on its largest axis
    const uint  span = max(hi.x - lo.x, hi.y - lo.y) + 1;
    
    const uint mip = min(span <= 1 ? 0 : firstbithigh(span - 1) + 1, mipCount - 1);//ceil(log2(span))
    
    //Find farthest in resulting 2*2 region
    const uint2 mipSize = max(size >> mip, uint2(1, 1));
    const uint2 a = min(lo >> mip, mipSize - 1);
    const uint2 b = min(hi >> mip, mipSize - 1);
    const float farthest = min(min(pyramid.Load(int3(a.x, a.y, mip)).r, pyramid.Load(int3(b.x, a.y, mip)).r),
                               min(pyramid.Load(int3(a.x, b.y, mip)).r, pyramid.Load(int3(b.x, b.y, mip)).r));
    return nearest < farthest;
}