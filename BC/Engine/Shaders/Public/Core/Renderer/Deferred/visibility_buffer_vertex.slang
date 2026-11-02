module visibility_buffer_vertex;
import GPURendererConstants;
import Vertex;

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

public extension Triangle {
    /// @return false for a visibility-buffer texel no triangle covers.
    public bool isValid() {
        return drawIndex != VISIBILITY_CLEAR;
    }

    /// @return triangle stored in a visibility-buffer texel.
    public static Triangle fromTexel(uint2 texel) {
        return PackedVisPrimitive(texel).unpack();
    }

    /// @return visibility-buffer texel holding this triangle.
    public uint2 toTexel() {
        return PackedVisPrimitive(this).toTexel();
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

    /// @param triangle triangle to pack
    public __init(Triangle triangle) {
        static_assert(MAX_TRIANGLES <= (1u << TRIANGLE_BITS), "triangle index does not fit TRIANGLE_BITS");
        drawIndex = triangle.drawIndex;
        meshletAndTriangleIndex = (triangle.meshletIndex << TRIANGLE_BITS) | triangle.triangleIndex;
    }

    /// @param texel visibility-buffer texel
    public __init(uint2 texel) {
        drawIndex = texel.x;
        meshletAndTriangleIndex = texel.y;
    }

    /// @return the packed triangle.
    public Triangle unpack() {
        Triangle result;
        result.drawIndex = drawIndex;
        result.meshletIndex = meshletAndTriangleIndex >> TRIANGLE_BITS;
        result.triangleIndex = meshletAndTriangleIndex & TRIANGLE_MASK;
        return result;
    }

    /// @return visibility-buffer texel holding this record.
    public uint2 toTexel() {
        return uint2(drawIndex, meshletAndTriangleIndex);
    }
};