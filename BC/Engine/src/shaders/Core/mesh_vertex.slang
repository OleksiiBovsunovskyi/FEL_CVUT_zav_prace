module mesh_vertex;

/// Interface between meshMain in Core/mesh.slang and fragMain in
/// Core/mesh_frag.slang; varying locations follow declaration order.
public struct MeshVertex {
    public float4 position : SV_Position;
    public float3 worldPosition : WORLDPOS;
    public float3 normal   : NORMAL;
    public float4 tangent  : TANGENT;
    public float2 texCoord : TEXCOORD;
    public nointerpolation uint materialIndex : MATERIAL;
    /// Meshlet the triangle came from, mixed with the instance index.
    public nointerpolation uint meshletId : MESHLETID;
    /// Sphere the meshlet is culled by: xyz = centre in world space, w = radius.
    public nointerpolation float4 meshletSphere : MESHLETSPHERE;
};
