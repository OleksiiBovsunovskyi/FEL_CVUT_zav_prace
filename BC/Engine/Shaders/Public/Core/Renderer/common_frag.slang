module common_frag;

import GPUMaterial;
import Vertex;

/// Bindless textures, indexed by GPUMaterial::textures. Slots are assigned only by TextureManager.
[[vk::binding(0, 0)]] public Sampler2D materialTextures[];

/// The one light in the scene, until GPULight and its per-frame buffer exist.
public static const float3 LIGHT_DIRECTION = normalize(float3(0.4, 0.8, 0.3));
public static const float  AMBIENT         = 0.15;

/**
 * @param slot bindless slot of the texture
 * @param uv texture coordinate with its per-pixel derivatives
 * @return texture filtered over the footprint of uv.
 * @note slot may be nonuniform across a wave.
 */
public float4 sampleTexture(uint slot, InterpolatedFloat2 uv) {
    return materialTextures[NonUniformResourceIndex(slot)].SampleGrad(uv.value, uv.ddx, uv.ddy);
}

/// @return value with its derivatives across the 2x2 pixel quad.
public InterpolatedFloat2 withQuadDerivatives(float2 value) {
    InterpolatedFloat2 result;
    result.value = value;
    result.ddx   = ddx(value);
    result.ddy   = ddy(value);
    return result;
}

/// @return Blinn-Phong specular power of a glTF roughness.
public float shininessFrom(float roughness) {
    const float alpha = max(roughness * roughness, 1e-3);
    return max(2.0 / (alpha * alpha) - 2.0, 1.0);
}

/**
 * @param material material of the surface
 * @param uv texture coordinate with its per-pixel derivatives
 * @return material albedo, multiplied by the albedo texture when the material has one.
 */
public float4 sampleAlbedo(GPUMaterial material, InterpolatedFloat2 uv) {
    float4 albedo = material.albedo;
    if (material.textures.x != INVALID_TEXTURE_INDEX)
        albedo *= sampleTexture(material.textures.x, uv);
    return albedo;
}

/**
 * @param material material of the surface
 * @param uv texture coordinate with its per-pixel derivatives
 * @param normal interpolated vertex normal
 * @param tangent interpolated vertex tangent; w is handedness
 * @return unit shading normal.
 */
public float3 sampleNormal(GPUMaterial material, InterpolatedFloat2 uv, float3 normal, float4 tangent) {
    normal = normalize(normal);
    if (material.textures.y == INVALID_TEXTURE_INDEX)
        return normal;
    // Gram-Schmidt: interpolation leaves the tangent off the normal plane.
    const float3 t = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
    const float3 b = cross(normal, t) * tangent.w;
    const float3 sampled = sampleTexture(material.textures.y, uv).xyz * 2.0 - 1.0;
    return normalize(t * sampled.x + b * sampled.y + normal * sampled.z);
}

/**
 * Blinn-Phong lighting from LIGHT_DIRECTION and AMBIENT, plus emission.
 * @param material material of the surface
 * @param uv texture coordinate with its per-pixel derivatives
 * @param albedo result of sampleAlbedo()
 * @param normal result of sampleNormal()
 * @param view unit vector from the surface to the camera
 * @return lit colour. //TODO: Make it take all the colors it need instead of sampling directly
 */
public float3 shadeBlinnPhong(GPUMaterial material, InterpolatedFloat2 uv, float3 albedo, float3 normal, float3 view) {
    // surface: x=specular intensity, y=shininess, z=metallic, w=roughness.
    float occlusion = 1.0;
    float shininess = material.surface.y;
    if (material.textures.z != INVALID_TEXTURE_INDEX) {
        // glTF packs occlusion in r, roughness in g, metallic in b.
        const float3 orm = sampleTexture(material.textures.z, uv).xyz;
        occlusion = orm.x;
        shininess = shininessFrom(orm.y * material.surface.w);
    }

    const float3 halfway = normalize(LIGHT_DIRECTION + view);
    const float diffuse  = max(dot(normal, LIGHT_DIRECTION), 0.0);
    const float specular = (diffuse > 0.0)
                         ? pow(max(dot(normal, halfway), 0.0), shininess) * material.surface.x
                         : 0.0;

    // emissive.w is the intensity the colour is scaled by.
    float3 emissive = float3(0.0);
    if ((material.flags & MATERIAL_EMISSIVE) != 0) {
        emissive = material.emissive.rgb * material.emissive.w;
        if (material.textures.w != INVALID_TEXTURE_INDEX)
            emissive *= sampleTexture(material.textures.w, uv).xyz;
    }

    return albedo * (AMBIENT * occlusion + diffuse) + specular + emissive;
}
