import gpu_types;
import mesh_vertex;

[[vk::push_constant]] ConstantBuffer<GPUMeshDrawPush> pc;

// Bindless textures, indexed by GPUMaterial::textures. Owned by TextureManager,
// which is the only thing that assigns a slot.
[[vk::binding(0, 0)]] Sampler2D textures[];

// The one light in the scene, until GPULight and its per-frame buffer exist.
static const float3 LIGHT_DIRECTION = normalize(float3(0.4, 0.8, 0.3));
static const float  AMBIENT         = 0.15;

// materialIndex is nonuniform across a wave, so every sample of the bindless
// array needs the qualifier.
float4 sampleTexture(uint slot, float2 uv) {
    return textures[NonUniformResourceIndex(slot)].Sample(uv);
}

// glTF stores roughness; Blinn-Phong takes a specular power.
float shininessFrom(float roughness) {
    const float alpha = max(roughness * roughness, 1e-3);
    return max(2.0 / (alpha * alpha) - 2.0, 1.0);
}

[shader("fragment")]
float4 fragMain(MeshVertex input) : SV_Target {
    const GPUMaterial material = pc.materials[input.materialIndex];

    float4 albedo = material.albedo;
    if (material.textures.x != INVALID_TEXTURE_INDEX)
        albedo *= sampleTexture(material.textures.x, input.texCoord);

    if ((material.flags & MATERIAL_ALPHA_MASK) != 0 &&
        albedo.a < material.alphaThreshold)
        discard;

    float3 normal = normalize(input.normal);
    if (material.textures.y != INVALID_TEXTURE_INDEX) {
        const float3 tangent = normalize(input.tangent.xyz);
        // Gram-Schmidt: interpolation leaves the tangent off the normal plane.
        const float3 t = normalize(tangent - normal * dot(normal, tangent));
        const float3 b = cross(normal, t) * input.tangent.w;
        const float3 sampled =
            sampleTexture(material.textures.y, input.texCoord).xyz * 2.0 - 1.0;
        normal = normalize(t * sampled.x + b * sampled.y + normal * sampled.z);
    }

    // x=specular intensity, y=shininess, z=metallic, w=roughness.
    float occlusion = 1.0;
    float shininess = material.surface.y;
    if (material.textures.z != INVALID_TEXTURE_INDEX) {
        // glTF packs occlusion in r, roughness in g, metallic in b.
        const float3 orm = sampleTexture(material.textures.z, input.texCoord).xyz;
        occlusion = orm.x;
        shininess = shininessFrom(orm.y * material.surface.w);
    }

    const float3 view    = normalize(pc.cameraPosition.xyz - input.worldPosition);
    const float3 halfway = normalize(LIGHT_DIRECTION + view);

    const float diffuse  = max(dot(normal, LIGHT_DIRECTION), 0.0);
    const float specular = (diffuse > 0.0)
                         ? pow(max(dot(normal, halfway), 0.0), shininess) *
                               material.surface.x
                         : 0.0;

    // emissive.w is the intensity the colour is scaled by.
    float3 emissive = float3(0.0);
    if ((material.flags & MATERIAL_EMISSIVE) != 0) {
        emissive = material.emissive.rgb * material.emissive.w;
        if (material.textures.w != INVALID_TEXTURE_INDEX)
            emissive *= sampleTexture(material.textures.w, input.texCoord).xyz;
    }

    const float3 color = albedo.rgb * (AMBIENT * occlusion + diffuse) +
                         specular + emissive;
    return float4(color, albedo.a);
}
