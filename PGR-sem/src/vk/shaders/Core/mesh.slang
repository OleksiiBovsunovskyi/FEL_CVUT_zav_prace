import gpu_types;

[[vk::push_constant]] ConstantBuffer<GPUMeshDrawPush> pc;

struct MeshVertex { 
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    nointerpolation uint materialIndex : MATERIAL;
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
        verts[i].normal        = normalize(mul(normalMatrix, vertex.normal.xyz));
        verts[i].materialIndex = mesh.material.value;
    }

    for (uint i = laneId; i < meshlet.triangleCount; i += 64) {
        // One triangle per uint: i0 | (i1 << 8) | (i2 << 16).
        const uint packed = mesh.meshletTriangles[meshlet.triangleOffset + i].packed;
        tris[i] = uint3(packed & 0xffu, (packed >> 8) & 0xffu, (packed >> 16) & 0xffu);
    }
}

// ponytail: one hardcoded light, no view vector, so Lambert only. Replace both
// with the Lights buffer and a camera position once GPULight exists.
static const float3 LIGHT_DIRECTION = normalize(float3(0.4, 0.8, 0.3));
static const float  AMBIENT         = 0.15;

[shader("fragment")]
float4 fragMain(MeshVertex input) : SV_Target {
    const GPUMaterial material = pc.materials[input.materialIndex];

    if ((material.flags & MATERIAL_ALPHA_MASK) != 0 &&
        material.albedo.a < material.alphaThreshold)
        discard;

    const float3 normal  = normalize(input.normal);
    const float  diffuse = max(dot(normal, LIGHT_DIRECTION), 0.0);

    // emissive.w is the intensity the colour is scaled by.
    const float3 emissive = ((material.flags & MATERIAL_EMISSIVE) != 0)
                          ? material.emissive.rgb * material.emissive.w
                          : float3(0.0);

    const float3 color = material.albedo.rgb * (AMBIENT + diffuse) + emissive;
    return float4(color, material.albedo.a);
}
