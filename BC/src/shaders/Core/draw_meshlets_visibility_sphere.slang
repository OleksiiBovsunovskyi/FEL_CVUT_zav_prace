module draw_meshlets_visibility_sphere;

/// Position of a point inside the sphere a meshlet is culled by, as a colour
/// ramp: blue at the centre, green at half the radius, red at the surface.
///
/// @param worldPosition fragment position in world space.
/// @param sphere xyz = centre in world space, w = radius.
/// @note A meshlet whose fragments never reach red has a sphere wider than its
///       geometry.
public float3 sphereTightness(float3 worldPosition, float4 sphere) {
    const float t = saturate(distance(worldPosition, sphere.xyz) / sphere.w);
    const float s = 2.0 * t - 1.0;
    return float3(saturate(s), 1.0 - abs(s), saturate(-s));
}
