import GPUTypes;
import mesh_vertex;
import SphereVisibilityTest;
import GPURendererConstants;
import common_mesh;

[[vk_push_constant]] ConstantBuffer<GPUForwardMeshDrawPush> pc;
[[vk_binding(0, 0)]] Sampler2D textures[];

// One workgroup per meshlet; build_draw_commands.slang sets groupCountX.
[shader("mesh")]
[numthreads(MESH_GROUP_SIZE, 1, 1)]
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

    const float4 meshletSphere = worldSphere(instance.transform, meshlet.boundingSphere);
    const bool visible = isMeshletVisible(pc.frame, textures[pc.depthPyramidSlot], meshletSphere);

    SetMeshOutputCounts(visible ? meshlet.vertexCount  : 0,
                        visible ? meshlet.triangleCount : 0);

    if (laneId == 0) incrementMetricCounters(pc.frame, meshlet, visible);
    if (!visible) return;
    const float4x4 mvp = mul(pc.frame->camera.viewProj, instance.transform);
    const float3x3 model = float3x3(instance.transform);
    // Inverse transponse 
    const float3x3 normalMatrix = float3x3(cross(model[1], model[2]),
                                           cross(model[2], model[0]),
                                           cross(model[0], model[1]));

    for (uint i = laneId; i < meshlet.vertexCount; i += MESH_GROUP_SIZE) {
        const GPUVertex vertex = getMeshletVertex(mesh, meshlet, i);

        verts[i].position      = mul(mvp, float4(vertex.position.xyz, 1.0));
        verts[i].worldPosition = mul(instance.transform, float4(vertex.position.xyz, 1.0)).xyz;
        verts[i].normal        = normalize(mul(normalMatrix, vertex.normal.xyz));
        // w is handedness and survives the transform unchanged.
        verts[i].tangent       = float4(mul(model, vertex.tangent.xyz),
                                        vertex.tangent.w);
        verts[i].texCoord      = vertex.texCoord;
        verts[i].materialIndex = mesh.material.value;
        verts[i].meshletId     = groupId ^ (draw.instanceIndex * 0x9E3779B9u);
        verts[i].meshletSphere = meshletSphere;
    }

    for (uint i = laneId; i < meshlet.triangleCount; i += MESH_GROUP_SIZE)
        tris[i] = getMeshletTriangle(mesh, meshlet, i);
}
