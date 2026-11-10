/** Shader-side unpacking of Meshlet. */
module meshlet_unpack;

__exported import geometry_common;

public extension Meshlet {
    /**
     * @param grid grid of the owning model
     * @return sphere centre in mesh space in xyz, radius in mesh space units in w.
     */
    public float4 unpackBoundingSphere(PositionGrid grid) {
        float3 center = grid.dequantize(base + GridPoint(int(sphereX), int(sphereY), int(sphereZ)));
        return float4(center, float(sphereRadius) * grid.step);
    }

    /// @return cone axis in xyz, length is within rounding of 1; cosine cutoff in w.
    public float4 unpackNormalCone() {
        return float4(decodeSnorm8(packed_normalCone), decodeSnorm8(packed_normalCone >> 8),
                      decodeSnorm8(packed_normalCone >> 16), decodeSnorm8(packed_normalCone >> 24));
    }
};
