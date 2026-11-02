import visibility_buffer_vertex;
import Vertex;

[shader("fragment")]
uint2 fragMain(VisVertex vertex, uint triangleIndex : SV_PrimitiveID) : SV_Target
{
    Triangle primitive;
    primitive.drawIndex     = vertex.drawIndex;
    primitive.meshletIndex  = vertex.meshletIndex;
    primitive.triangleIndex = triangleIndex;
    return primitive.toTexel();
}