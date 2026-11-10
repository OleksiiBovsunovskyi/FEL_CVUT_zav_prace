import GPUDeferredDrawPush;
import visibility_buffer_vertex;
import GPUDraw;
import meshlet_geometry;
import common_frag;

[[vk_push_constant]] GPUDeferredDrawPush pc;
[[vk_binding(0, 0)]] Sampler2D sampled_textures[];
[[vk_binding(0, 0)]] Sampler2D<uint2> textures[];

/// Matches FullscreenVertex in deferred_mesh.slang.
struct FullscreenVertex {
    float4 position : SV_Position;
    /// (0,0) at the top-left of the screen, (1,1) at the bottom-right.
    float2 texCoord : TEXCOORD0;
};

[shader("fragment")]
float4 fragMain(FullscreenVertex input) : SV_Target
{
    uint w, h, levels;
    textures[pc.visibilityBufferSlot].GetDimensions(0, w, h, levels);
    int2 coord = int2(floor(input.texCoord * float2(w, h)));
    coord = clamp(coord, int2(0), int2(w - 1, h - 1));
    TriangleId prim = TriangleId.fromTexel(textures[pc.visibilityBufferSlot].Load(int3(coord,0)));
    if (!prim.isValid()) //Invalid visibility texture pixels are background, no triangles are there
        return float4(1, 0, 0, 0);
    //Here we have triangle, lets find its data
    GPUDrawData drawData = pc.drawData[prim.drawIndex];
    Mesh mesh = drawData.mesh[0];

    Array<UnpackedVertex, 3> triVerts = prim.getVertices(pc.drawData);
    //Compute barycenters and derivatives with some magic
    TriangleBarycentrics bars =
    TriangleBarycentrics.fromVertices(triVerts,
                                      prim.getModelViewProjection(
                                                    pc.frame.camera.viewProj,
                                                    pc.drawData,
                                                    pc.instances),
                                       input.position.xy,float2(w,h));
    //We need to interpolate vertex uvs also
    InterpolatedFloat2 uv = bars.interpolateWithDerivatives(triVerts[0].texCoord, triVerts[1].texCoord, triVerts[2].texCoord);
    // And other vertext attributes
    float3 normal = bars.interpolate(triVerts[0].normal.xyz, triVerts[1].normal.xyz, triVerts[2].normal.xyz);
    float3 tangent_xyz = bars.interpolate(triVerts[0].tangent.xyz, triVerts[1].tangent.xyz, triVerts[2].tangent.xyz);
        //w should be the same for each vert
    float4 tangent = float4(tangent_xyz, triVerts[0].tangent.w);

    //Now we can sample textures with interpolated uvs
    float4 albedo_tex = sampleAlbedo(pc.materials[mesh.material.value], uv);
    float3 normal_tex = sampleNormal(pc.materials[mesh.material.value], uv, normal, tangent);
    // Phong function samples ORM itself.
    // Phong also needs view direction to the surface.
    float3 interpolated_vertex_mesh_pos =
        bars.interpolate(triVerts[0].position.xyz,
                         triVerts[1].position.xyz,
                         triVerts[2].position.xyz);

    float4 interpolated_vertex_world_pos = mul(
        pc.instances[drawData.instanceIndex].unpackTransform(),
        float4(interpolated_vertex_mesh_pos, 1));
    float3 view_direction = (normalize( pc.frame.camera.position.xyz - interpolated_vertex_world_pos.xyz ));


    float3 color = shadeBlinnPhong(pc.materials[mesh.material.value], uv,
    albedo_tex.xyz, normal_tex, view_direction);
    //we takle alpha from the albedo alpha
    return float4(color, albedo_tex.w);
}