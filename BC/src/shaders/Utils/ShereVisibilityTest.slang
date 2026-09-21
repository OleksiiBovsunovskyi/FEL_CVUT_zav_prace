module ShereVisibilityTest;

// Not public: visible only inside this module.
bool outside(float4 plane, float3 center, float radius) {
    return dot(plane.xyz, center) + plane.w < -radius * length(plane.xyz);
}

/// Frustum test for a bounding sphere, from the six planes of the clip matrix
/// (Gribb-Hartmann). Conservative: a sphere straddling a plane counts visible.
///
/// @param viewProj world -> clip.
/// @param model model -> world; its largest axis scale is applied to the radius.
/// @param boundingSphere xyz = centre in model space, w = radius.
public bool sphereVisible(float4x4 viewProj, float4x4 model, float4 boundingSphere) {
    float3 center = mul(model, float4(boundingSphere.xyz, 1.0)).xyz;

    float sx = length(float3(model[0][0], model[1][0], model[2][0]));
    float sy = length(float3(model[0][1], model[1][1], model[2][1]));
    float sz = length(float3(model[0][2], model[1][2], model[2][2]));
    float radius = boundingSphere.w * max(sx, max(sy, sz));

    // Indexing a matrix yields a row here, so the planes need no gathering.
    float4 r0 = viewProj[0];
    float4 r1 = viewProj[1];
    float4 r2 = viewProj[2];
    float4 r3 = viewProj[3];

    if (outside(r3 + r0, center, radius)) return false;
    if (outside(r3 - r0, center, radius)) return false;
    if (outside(r3 + r1, center, radius)) return false;
    if (outside(r3 - r1, center, radius)) return false;
    if (outside(r2,      center, radius)) return false;
    if (outside(r3 - r2, center, radius)) return false;
    return true;
}
