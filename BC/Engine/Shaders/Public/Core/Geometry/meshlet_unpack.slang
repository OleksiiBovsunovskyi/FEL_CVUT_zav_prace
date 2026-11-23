/** Shader-side unpacking of Meshlet. */
module meshlet_unpack;

__exported import geometry_common;
import BitLib;

public extension Meshlet {
    /// @return offset bits on x, y and z, 1..MESHLET_MAX_POSITION_BITS each.
    public uint3 positionBits() {
        return uint3(positionBitsMinus1X, positionBitsMinus1Y, positionBitsMinus1Z) + 1;
    }

    /**
     * @param positions position stream of the owning mesh
     * @param i vertex within the meshlet, 0..vertexCount-1
     * @return grid point of vertex i.
     */
    public GridPoint unpackGridPoint(Ptr<PositionWord> positions, uint i) {
        const uint3 bits = positionBits();
        const uint bitOffset = i * (bits.x + bits.y + bits.z);
        const uint3 offset = uint3(readBits(positions, firstPositionWord, bitOffset, bits.x),
                                   readBits(positions, firstPositionWord, bitOffset + bits.x, bits.y),
                                   readBits(positions, firstPositionWord, bitOffset + bits.x + bits.y, bits.z));
        return base + (GridPoint(offset) << int(stepExponent));
    }

    /**
     * @param positions position stream of the owning mesh
     * @param grid grid of the owning model
     * @param i vertex within the meshlet, 0..vertexCount-1
     * @return mesh space position of vertex i.
     */
    public float3 unpackPosition(Ptr<PositionWord> positions, PositionGrid grid, uint i) {
        return grid.dequantize(unpackGridPoint(positions, i));
    }

    /// @return log2 of the bounding sphere unit, in grid steps.
    public uint sphereShift() {
        const uint3 bits = positionBits();
        const uint widest = max(bits.x, max(bits.y, bits.z));
        return stepExponent + (widest > MESHLET_SPHERE_OFFSET_BITS ? widest - MESHLET_SPHERE_OFFSET_BITS : 0);
    }

    /**
     * @param grid grid of the owning model
     * @return sphere centre in mesh space in xyz, radius in mesh space units in w.
     */
    public float4 unpackBoundingSphere(PositionGrid grid) {
        const uint shift = sphereShift();
        const float3 center = grid.dequantize(base + (GridPoint(int(sphereX), int(sphereY), int(sphereZ)) << int(shift)));
        return float4(center, float(sphereRadius) * grid.step * float(1u << shift));
    }

    /// @return cone axis in xyz, length is within rounding of 1; cosine cutoff in w.
    public float4 unpackNormalCone() {
        return float4(decodeSnorm8(packed_normalCone), decodeSnorm8(packed_normalCone >> 8),
                      decodeSnorm8(packed_normalCone >> 16), decodeSnorm8(packed_normalCone >> 24));
    }
};
