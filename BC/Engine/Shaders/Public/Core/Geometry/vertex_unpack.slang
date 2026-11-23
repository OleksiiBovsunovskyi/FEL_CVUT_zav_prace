/** Shader-side unpacking of Vertex. */
module vertex_unpack;

__exported import geometry_common;

public extension Vertex {
    public float3 unpackNormal() {
        return octDecode(float2(decodeSnorm11(normalX), decodeSnorm11(normalY)));
    }

    /// @return unit tangent in xyz, bitangent handedness (+1 / -1) in w.
    public float4 unpackTangent() {
        float3 axis0, axis1;
        tangentPlaneAxes(unpackNormal(), axis0, axis1);
        float angle = float(tangentAngle) / 256.0 * TWO_PI; //TODO: 256 DEPENDS ON THE TANGENT BITS IN THE VERTEX REPRESENTATION
        return float4(axis0 * cos(angle) + axis1 * sin(angle), tangentSign != 0 ? -1.0 : 1.0);
    }

    public float2 unpackTexCoord() {
        return float2(f16tof32(packed_texCoord), f16tof32(packed_texCoord >> 16));
    }
};
