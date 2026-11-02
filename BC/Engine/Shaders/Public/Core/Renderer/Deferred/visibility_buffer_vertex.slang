module visibility_buffer_vertex;
import GPURendererConstants;

/// Vertex output of the visibility-buffer mesh shader.
public struct VisVertex {
    public float4 position : SV_Position;
    /// Index into drawData; SV_DrawIndex of the visibility pass.
    public nointerpolation uint drawIndex : DRAWINDEX;
    /// Meshlet within the draw's mesh.
    public nointerpolation uint meshletIndex : MESHLETINDEX;
};

/// Per-primitive output of the visibility-buffer mesh shader.
public struct VisTriangle {
    /// Triangle within the meshlet, 0..MAX_TRIANGLES-1.
    public uint triangleIndex : SV_PrimitiveID;
};

/// Triangle a visibility-buffer pixel is covered by.
public struct VisPrimitive {
    /// Index into drawData; SV_DrawIndex of the visibility pass.
    public nointerpolation uint drawIndex : DRAWINDEX;
    /// Meshlet within the draw's mesh.
    public nointerpolation uint meshletIndex : MESHLETINDEX;
    /// Triangle within the meshlet, 0..MAX_TRIANGLES-1.
    public nointerpolation uint triangleIndex : TRIANGLEINDEX;

    /// @return false for a texel no triangle covers.
    public bool isValid() {
        return drawIndex != VISIBILITY_CLEAR;
    }
};

/// Low bits of PackedVisPrimitive::meshletAndTriangleIndex holding the triangle.
public static const uint TRIANGLE_BITS = 6;
public static const uint TRIANGLE_MASK = (1u << TRIANGLE_BITS) - 1u;

public struct PackedVisPrimitive {
    /// Index into drawData.
    public nointerpolation uint drawIndex : DRAWINDEX;
    /**
     * Meshlet within the draw's mesh and triangle within the meshlet.
     * Bits 31..TRIANGLE_BITS hold the meshlet, bits TRIANGLE_BITS-1..0 the triangle.
     */
    public nointerpolation uint meshletAndTriangleIndex : MESHLETANDTRIANGLEINDEX;
};

public VisPrimitive unpackVisPrimitive(PackedVisPrimitive packed)
{
    VisPrimitive result;
    result.drawIndex = packed.drawIndex;
    result.meshletIndex = packed.meshletAndTriangleIndex >> TRIANGLE_BITS;
    result.triangleIndex = packed.meshletAndTriangleIndex & TRIANGLE_MASK;
    return result;
}
public PackedVisPrimitive packVisPrimitive(VisPrimitive primitive)
{
    static_assert(MAX_TRIANGLES <= (1u << TRIANGLE_BITS), "triangle index does not fit TRIANGLE_BITS");
    PackedVisPrimitive result;
    result.drawIndex = primitive.drawIndex;
    result.meshletAndTriangleIndex = (primitive.meshletIndex << TRIANGLE_BITS) | primitive.triangleIndex;
    return result;
}
public VisPrimitive fromTexel(uint2 texel) {
    PackedVisPrimitive packed;
    packed.drawIndex = texel.x;
    packed.meshletAndTriangleIndex = texel.y;
    return unpackVisPrimitive(packed);
}
public uint2 toTexel(VisPrimitive primitive) {
    PackedVisPrimitive packed = packVisPrimitive(primitive);
    return uint2(packed.drawIndex, packed.meshletAndTriangleIndex);
}