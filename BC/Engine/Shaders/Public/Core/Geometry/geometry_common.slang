/** Decoding helpers shared by the unpackers of the packed Geometry types. */
module geometry_common;
import PositionGrid;
__exported import Geometry;

public static const float TWO_PI = 6.28318530718;

/// @return unit vector of an octahedral encoding, -1..1 per component.
public float3 octDecode(float2 p) {
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

/**
 * Unit axes spanning the plane perpendicular to normal.
 * @param normal unit normal
 */
public void tangentPlaneAxes(float3 normal, out float3 axis0, out float3 axis1) {
    float side = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (side + normal.z);
    float b = normal.x * normal.y * a;
    axis0 = float3(1.0 + side * normal.x * normal.x * a, side * b, -side * normal.x);
    axis1 = float3(b, side + normal.y * normal.y * a, -normal.y);
}

/// @param q 11-bit snorm, value = (q - 1023) / 1023
public float decodeSnorm11(uint q) {
    return (float(q) - 1023.0) / 1023.0;
}

/// @param q 8-bit snorm in the low byte, as written by glm::packSnorm4x8
public float decodeSnorm8(uint q) {
    return max(float(int(q << 24) >> 24) / 127.0, -1.0);
}

/// @param q 15-bit quaternion component over -1/sqrt(2)..1/sqrt(2)
public float decodeQuaternionComponent(uint q) {
    return (float(q) / 32767.0 * 2.0 - 1.0) * 0.70710678118;
}

public extension PositionGrid {
    /// @return mesh space position of the grid point.
    public float3 dequantize(GridPoint point) {
        return origin + float3(point) * step;
    }
};
