import GPUTypes;
import mesh_vertex;
import draw_meshlets;
import draw_meshlets_visibility_sphere;
import common_frag;
import Vertex;

[[vk::push_constant]] ConstantBuffer<GPUForwardMeshDrawPush> pc;

[shader("fragment")]
float4 fragMain(MeshVertex input) : SV_Target {
    if (pc.frame->debugShowMeshletSpheres)
        return float4(sphereTightness(input.worldPosition, input.meshletSphere), 1.0);

    if (pc.frame->debugShowMeshlets) {
        const float shade = 0.4 + 0.6 * max(dot(normalize(input.normal), LIGHT_DIRECTION), 0.0);
        return float4(meshletColor(input.meshletId) * shade, 1.0);
    }   

    const GPUMaterial material = pc.materials[input.materialIndex];
    const InterpolatedFloat2 uv = withQuadDerivatives(input.texCoord);

    const float4 albedo = sampleAlbedo(material, uv);
    if ((material.flags & MATERIAL_ALPHA_MASK) != 0 &&
        albedo.a < material.alphaThreshold)
        discard;

    const float3 normal = sampleNormal(material, uv, input.normal, input.tangent);
    if(pc.frame->debugDrawNormals)
        return float4(normal, 1.0);

    const float3 view  = normalize(pc.frame->camera.position.xyz - input.worldPosition);
    const float3 color = shadeBlinnPhong(material, uv, albedo.rgb, normal, view);
    return float4(color, albedo.a);
}
