import gpu_types;
import mesh_vertex;

[[vk::push_constant]] ConstantBuffer<GPUMeshDrawPush> pc;


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
    const GPUMeshInstance instance = pc.instances[draw.instanceIndex];
    // Every section of the mesh is reached through its header.
    const GPUMeshHeader mesh = draw.mesh[0];

    const GPUMeshlet meshlet = mesh.meshlets[groupId];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    const float4x4 mvp = mul(pc.viewProj, instance.transform);
    // Uniform scale is assumed; a non-uniform one needs the inverse transpose.
    const float3x3 normalMatrix = float3x3(instance.transform);

    for (uint i = laneId; i < meshlet.vertexCount; i += 64) {
        const uint      index  = mesh.meshletVertexIndices[meshlet.vertexOffset + i].value;
        const GPUVertex vertex = mesh.vertices[index];

        verts[i].position      = mul(mvp, float4(vertex.position.xyz, 1.0));
        verts[i].worldPosition = mul(instance.transform, float4(vertex.position.xyz, 1.0)).xyz;
        verts[i].normal        = normalize(mul(normalMatrix, vertex.normal.xyz));
        // w is handedness and survives the transform unchanged.
        verts[i].tangent       = float4(mul(normalMatrix, vertex.tangent.xyz),
                                        vertex.tangent.w);
        verts[i].texCoord      = vertex.texCoord;
        verts[i].materialIndex = mesh.material.value;
    }

    for (uint i = laneId; i < meshlet.triangleCount; i += 64) {
        // One triangle per uint: i0 | (i1 << 8) | (i2 << 16).
        const uint packed = mesh.meshletTriangles[meshlet.triangleOffset + i].packed;
        tris[i] = uint3(packed & 0xffu, (packed >> 8) & 0xffu, (packed >> 16) & 0xffu);
    }
}
