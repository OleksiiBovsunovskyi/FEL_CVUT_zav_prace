/// Vertex output of the deferred fullscreen quad.
struct FullscreenVertex {
    float4 position : SV_Position;
    /// (0,0) at the top-left of the screen, (1,1) at the bottom-right.
    float2 texCoord : TEXCOORD0;
};

//Just draw screen quad
[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    out vertices FullscreenVertex verts[4],
    out indices  uint3            tris[2])
{
    SetMeshOutputCounts(4, 2);

    [ForceUnroll]
    for (uint i = 0; i < 4; ++i) {
        const float2 uv = float2(i & 1u, i >> 1u);
        verts[i].position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        verts[i].texCoord = uv;
    }

    tris[0] = uint3(0, 2, 1);
    tris[1] = uint3(1, 2, 3);
}
