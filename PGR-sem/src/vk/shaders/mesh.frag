#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "gpu_types.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 0) out vec4 fragColor;

void main() {
    GPUMesh mesh = pc.meshes.m[pc.meshIndex];
    GPUMaterial material = pc.materials.m[mesh.materialIndex];

    /**
     * Object space; the model matrix is folded into pc.viewProj, leaving a
     * rotated node lit as if unrotated. Needs per-object data to fix.
     */
    vec3 n = normalize(vNormal);

    const vec3 lightDir = normalize(vec3(0.4, 0.8, 0.5));   /* no light buffer yet */
    float ndl = max(dot(n, lightDir), 0.0);

    vec3 lit = material.albedo.rgb * (0.25 + 0.75 * ndl);
    if ((material.flags & MATERIAL_EMISSIVE) != 0u)
        lit += material.emissive.rgb * material.emissive.a;

    /* No manual sRGB encode; the swapchain format is _SRGB. */
    fragColor = vec4(lit, 1.0);
}
