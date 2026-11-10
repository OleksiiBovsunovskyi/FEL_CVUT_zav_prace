/** Shader-side unpacking of Instance. */
module instance_unpack;

__exported import geometry_common;

public extension Instance {
    public float3 unpackScale() {
        return float3(f16tof32(scaleX), f16tof32(scaleY), f16tof32(scaleZ));
    }

    /// @return unit rotation quaternion, xyzw.
    public float4 unpackRotation() {
        float stored[3] = { decodeQuaternionComponent(rotationA), decodeQuaternionComponent(rotationB),
                            decodeQuaternionComponent(rotationC) };
        float4 q = float4(0.0);
        int n = 0;
        for (int i = 0; i < 4; ++i) {
            if (i != int(rotationLargest)) q[i] = stored[n++];
        }
        q[rotationLargest] = sqrt(max(0.0, 1.0 - dot(q, q)));
        return normalize(q);
    }

    /// @return mesh space to world space matrix: translation * rotation * scale.
    public float4x4 unpackTransform() {
        const float4 q = unpackRotation();
        const float3 s = unpackScale();
        const float3 row0 = float3(1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y - q.w * q.z), 2.0 * (q.x * q.z + q.w * q.y));
        const float3 row1 = float3(2.0 * (q.x * q.y + q.w * q.z), 1.0 - 2.0 * (q.x * q.x + q.z * q.z), 2.0 * (q.y * q.z - q.w * q.x));
        const float3 row2 = float3(2.0 * (q.x * q.z - q.w * q.y), 2.0 * (q.y * q.z + q.w * q.x), 1.0 - 2.0 * (q.x * q.x + q.y * q.y));
        return float4x4(float4(row0 * s, position.x),
                        float4(row1 * s, position.y),
                        float4(row2 * s, position.z),
                        float4(0.0, 0.0, 0.0, 1.0));
    }
};
