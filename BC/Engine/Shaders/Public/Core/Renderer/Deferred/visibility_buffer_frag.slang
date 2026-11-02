import visibility_buffer_vertex;

[shader("fragment")]
uint2 fragMain(VisVertex vertex, uint triangleIndex : SV_PrimitiveID) : SV_Target
{
    VisPrimitive primitive;
    primitive.drawIndex     = vertex.drawIndex;
    primitive.meshletIndex  = vertex.meshletIndex;
    primitive.triangleIndex = triangleIndex;
    return toTexel(primitive);
}