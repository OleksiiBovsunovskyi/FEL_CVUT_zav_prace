import GPUVisibilityPass;
import GPURendererConstants;
import GPUFrame;
import GPUDraw;
import SphereVisibilityTest;
import visibility_buffer_vertex;
import common_mesh;
import meshlet_geometry;

[[vk_push_constant]] GPUVisibilityPassPush pc;
[[vk_binding(0, 0)]] Sampler2D textures[];

[shader("mesh")]
[numthreads(MESH_GROUP_SIZE, 1, 1)]
[outputtopology("triangle")]
void meshMain( uint  groupId : SV_GroupID,
                  uint  laneId  : SV_GroupIndex,
                  uint  drawId  : SV_DrawIndex,
                  out vertices   VisVertex          verts[MAX_VERTICES],
                  out indices    uint3              tris[MAX_TRIANGLES],
                  out primitives VisTriangle        prims[MAX_TRIANGLES]) {

    // Slot handed out by the atomic in build_draw_commands.slang.
    const GPUDrawData draw   = pc.drawData[drawId];

    const float4x4 model = pc.instances[draw.instanceIndex].unpackTransform();
    // Every section of the mesh is reached through its header.
    const Mesh mesh = draw.mesh[0];

    const Meshlet meshlet = mesh.meshlets[groupId];

    const float4 meshletSphere = worldSphere(model, meshlet.unpackBoundingSphere(mesh.grid));
    const bool visible = isMeshletVisible(pc.frame, textures[pc.depthPyramidSlot], meshletSphere);

    SetMeshOutputCounts(visible ? meshlet.vertexCount  : 0,
                        visible ? meshlet.triangleCount : 0);

    if (laneId == 0) incrementMetricCounters(pc.frame, meshlet, visible);
    if (!visible) return;
    const float4x4 mvp = mul(pc.frame->camera.viewProj, model);

    for (uint i = laneId; i < meshlet.vertexCount; i += MESH_GROUP_SIZE) {
        const Vertex vertex = mesh.vertices[meshlet.firstMeshletVertex + i];
        verts[i].position     = mul(mvp, float4(vertex.unpackPosition(mesh.grid, meshlet.base), 1.0));
        verts[i].drawIndex    = drawId;
        verts[i].meshletIndex = groupId;
    }

    for (uint i = laneId; i < meshlet.triangleCount; i += MESH_GROUP_SIZE) {
        tris[i] = getMeshletTriangle(mesh, meshlet, i);
        prims[i].triangleIndex = i;
    }
}