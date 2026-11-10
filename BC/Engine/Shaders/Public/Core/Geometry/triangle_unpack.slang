/** Shader-side unpacking of Triangle. */
module triangle_unpack;

__exported import geometry_common;

public extension Triangle {
    /// @return offsets of the triangle's vertices into the meshlet's vertices.
    public uint3 unpack() {
        return uint3(v0, v1, v2);
    }
};
