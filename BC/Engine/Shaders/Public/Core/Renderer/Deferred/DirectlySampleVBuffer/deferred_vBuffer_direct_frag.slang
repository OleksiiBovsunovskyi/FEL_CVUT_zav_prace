import GPUDeferredDrawPush;
import visibility_buffer_vertex;
import GPUDraw;
import GPUMesh;

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
    VisPrimitive prim = fromTexel(textures[pc.visibilityBufferSlot].Load(int3(coord,0)));
    if (!prim.isValid()) //Invalid visibility texture pixels are background, no triangles are there
        return float4(1, 0, 0, 0);
   //Here we have triangle, lets find its data
    GPUDrawData drawData = pc.drawData[prim.drawIndex];
    GPUMeshHeader mesh = drawData.mesh[0];
    GPUMeshlet meshlet = mesh.meshlets[prim.meshletIndex];

    float4 color = pc.materials[mesh.material.value].albedo;

    return color;
}