import gpu_types;

/// Mirror of MeshDrawPush in Core/GPUTypes.ixx.
struct MeshDrawPush {
    float4x4 viewProj;                  // 0

    GPUDrawData* drawData;              // 64
    GPUObject*   objects;               // 72
    GPUMesh*     meshes;                // 80
    GPUVertex*   vertices;              // 88
    GPUMeshlet*  meshlets;              // 96
    uint*        meshletVertexIndices;  // 104
    uint*        meshletTriangles;      // 112
};                                      // 120

[[vk::push_constant]] ConstantBuffer<MeshDrawPush> pc;

struct MeshVertex {
    float4 position : SV_Position;
    float3 normal   : NORMAL;
};

// meshopt's limits, matching MESHLET_MAX_* in ClusterLODGenerator.ixx.
static const uint MAX_VERTICES  = 64;
static const uint MAX_TRIANGLES = 64;

// One workgroup per meshlet; build_draw_commands.slang sets groupCountX.
[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    uint  groupId : SV_GroupID,
    uint  laneId  : SV_GroupIndex,
    uint  drawId  : SV_DrawIndex,
    out vertices MeshVertex verts[MAX_VERTICES],
    out indices  uint3      tris[MAX_TRIANGLES])
{
    // Slot handed out by the atomic in build_draw_commands.slang.
    const GPUDrawData draw   = pc.drawData[drawId];
    const GPUObject   object = pc.objects[draw.objectIndex];
    const GPUMesh     mesh   = pc.meshes[draw.meshIndex];

    const GPUMeshlet meshlet = pc.meshlets[mesh.meshletOffset + groupId];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    const float4x4 mvp = mul(pc.viewProj, object.transform);
    // Uniform scale is assumed; a non-uniform one needs the inverse transpose.
    const float3x3 normalMatrix = float3x3(object.transform);

    const uint vertexBase   = mesh.meshletVertexIndexOffset + meshlet.vertexOffset;
    const uint triangleBase = mesh.meshletTriangleOffset + meshlet.triangleOffset;

    for (uint i = laneId; i < meshlet.vertexCount; i += 64) {
        const uint      index  = pc.meshletVertexIndices[vertexBase + i];
        const GPUVertex vertex = pc.vertices[mesh.vertexOffset + index];

        verts[i].position = mul(mvp, float4(vertex.position.xyz, 1.0));
        verts[i].normal   = normalize(mul(normalMatrix, vertex.normal.xyz));
    }

    for (uint i = laneId; i < meshlet.triangleCount; i += 64) {
        // One triangle per uint: i0 | (i1 << 8) | (i2 << 16).
        const uint packed = pc.meshletTriangles[triangleBase + i];
        tris[i] = uint3(packed & 0xffu, (packed >> 8) & 0xffu, (packed >> 16) & 0xffu);
    }
}

[shader("fragment")]
float4 fragMain(MeshVertex input) : SV_Target {
    return float4(input.normal, 1.0);
}
