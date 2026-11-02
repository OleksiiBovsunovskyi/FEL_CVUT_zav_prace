module Vertex;
import GPUMesh;
import GPUMaterial;
import GPUDraw;
import GPUInstance;
import GPURendererConstants;

/**
* Vertex representation, not nessesarelly world space
*/
public struct Vertex {
    public float4 location : SV_Position;
    public float2 texCoord : TEXCOORD;
};

public struct MeshletVertexOffset {
    public uint offset;
}

/**
* Meshlet local triangle representation.
*/
public struct MeshletTriangle {
    public uint32_t packed;

    /*
    * unpacks triangle to meshlet local vertex offsets
    */
    public Array<MeshletVertexOffset, 3> getUnpacked() {
        return { { packed & 0xffu },
                 { (packed >> 8) & 0xffu },
                 { (packed >> 16) & 0xffu } };
    }
    /**Returns induvidual meshlet local vertex offsets*/
    public __subscript(uint i) -> MeshletVertexOffset {
        get {
        return { (packed >> (i * 8)) & 0xffu };
        }
        }
}

/**
 * @param i vertex within the meshlet, 0..meshlet.vertexCount-1
 * @return vertex i of the meshlet.
 */
public GPUVertex getMeshletVertex(GPUMeshHeader mesh, GPUMeshlet meshlet, uint i) {
    return mesh.vertices[mesh.meshletVertices[meshlet.firstMeshletVertex + i].value];
}

/**
 * @param i triangle within the meshlet, 0..meshlet.triangleCount-1
 * @return meshlet-local vertex indices of triangle i.
 */
public uint3 getMeshletTriangle(GPUMeshHeader mesh, GPUMeshlet meshlet, uint i) {
    const MeshletTriangle triangle = { mesh.meshletTriangles[meshlet.firstMeshletTriangle + i].packedTriangle };
    return uint3(triangle[0].offset, triangle[1].offset, triangle[2].offset);
}

/**Representation of the triangle in the geometry buffer*/
public struct Triangle {
    /// Index into drawData.
    public uint drawIndex;
    /// Meshlet within the draw's mesh.
    public uint meshletIndex;
    /// Triangle within the meshlet, 0..MAX_TRIANGLES-1.
    public uint triangleIndex;

    public Ptr<GPUMeshHeader> getMeshHeader(Ptr<GPUDrawData> drawData)
    {
        return drawData[drawIndex].mesh;
    }

    /**
     * @param drawData draws of the visibility pass
     * @return vertices 0, 1, 2 of the triangle, in mesh space.
     */
    public Array<GPUVertex, 3> getVertices(Ptr<GPUDrawData> drawData) {
        const GPUMeshHeader mesh = *getMeshHeader(drawData);
        const GPUMeshlet meshlet = mesh.meshlets[meshletIndex];
        const uint3 corners = getMeshletTriangle(mesh, meshlet, triangleIndex);
        return { getMeshletVertex(mesh, meshlet, corners.x),
                 getMeshletVertex(mesh, meshlet, corners.y),
                 getMeshletVertex(mesh, meshlet, corners.z) };
    }

    /**
     * @param viewProj view-projection
     * @param drawData draws produced this triangle
     * @param instances instances drawData indexes
     * @return mesh space to clip space transform of the triangle.
     */
    public float4x4 getModelViewProjection(float4x4 viewProj, Ptr<GPUDrawData> drawData, Ptr<GPUMeshInstance> instances) {
        return mul(viewProj, instances[drawData[drawIndex].instanceIndex].transform);
    }
}

/// Value of an interpolated float2 and its change over one pixel step.
public struct InterpolatedFloat2 {
    public float2 value;
    public float2 ddx;
    public float2 ddy;
}

/**
 * Perspective-correct barycentrics of one pixel in a triangle, with their per-pixel derivatives.
 * weights.x, weights.y and weights.z weigh vertices 0, 1 and 2.
 * ddx and ddy are the derivatives of weights over one pixel step in +x and +y; screen y points down.
 */
public struct TriangleBarycentrics {
    public float3 weights;
    public float3 ddx;
    public float3 ddy;

    /**
     * @param pointNdc point in NDC
     * @param vertex0Ndc vertex 0 in NDC
     * @param vertex1Ndc vertex 1 in NDC
     * @param vertex2Ndc vertex 2 in NDC
     * @param vertexInverseW 1 / clip w of vertices 0, 1 and 2
     * @return perspective-correct barycentric weights of pointNdc.
     */
    [Differentiable]
    static float3 perspectiveWeightsAt(float2 pointNdc, no_diff float2 vertex0Ndc, no_diff float2 vertex1Ndc,
                                       no_diff float2 vertex2Ndc, no_diff float3 vertexInverseW) {
        const float3 screenWeights = float3(edgeFunction(vertex1Ndc, vertex2Ndc, pointNdc),
                                            edgeFunction(vertex2Ndc, vertex0Ndc, pointNdc),
                                            edgeFunction(vertex0Ndc, vertex1Ndc, pointNdc));
        const float3 perspectiveWeights = screenWeights * vertexInverseW;
        return perspectiveWeights / (perspectiveWeights.x + perspectiveWeights.y + perspectiveWeights.z);
    }

    /// @return twice the signed area of the triangle edgeStart, edgeEnd, point.
    [Differentiable]
    static float edgeFunction(float2 edgeStart, float2 edgeEnd, float2 point) {
        const float2 edge = edgeEnd - edgeStart;
        const float2 toPoint = point - edgeStart;
        return edge.x * toPoint.y - edge.y * toPoint.x;
    }

    /**
     * @param clip0 clip-space position of vertex 0
     * @param clip1 clip-space position of vertex 1
     * @param clip2 clip-space position of vertex 2
     * @param pixel pixel centre in pixels, as SV_Position.xy
     * @param screenSize render target size in pixels
     */
    public __init(float4 clip0, float4 clip1, float4 clip2, float2 pixel, float2 screenSize) {
        const float3 vertexInverseW = rcp(float3(clip0.w, clip1.w, clip2.w));
        const float2 vertex0Ndc = clip0.xy * vertexInverseW.x;
        const float2 vertex1Ndc = clip1.xy * vertexInverseW.y;
        const float2 vertex2Ndc = clip2.xy * vertexInverseW.z;

        const float2 pixelNdc = pixel / screenSize * 2.0 - 1.0;
        const float2 ndcPerPixel = 2.0 / screenSize;
        const let weightsWithDdx = fwd_diff(perspectiveWeightsAt)(diffPair(pixelNdc, float2(ndcPerPixel.x, 0.0)),
                                                                  vertex0Ndc, vertex1Ndc, vertex2Ndc, vertexInverseW);
        const let weightsWithDdy = fwd_diff(perspectiveWeightsAt)(diffPair(pixelNdc, float2(0.0, ndcPerPixel.y)),
                                                                  vertex0Ndc, vertex1Ndc, vertex2Ndc, vertexInverseW);
        weights = weightsWithDdx.p;
        ddx = weightsWithDdx.d;
        ddy = weightsWithDdy.d;
    }

    /// @return attributes of vertices 0, 1 and 2 blended by weights.
    public float3 interpolate(float3 attribute0, float3 attribute1, float3 attribute2) {
        return attribute0 * weights.x + attribute1 * weights.y + attribute2 * weights.z;
    }

    /// @return attributes of vertices 0, 1 and 2 blended by weights, with per-pixel derivatives for SampleGrad.
    public InterpolatedFloat2 interpolateWithDerivatives(float2 attribute0, float2 attribute1, float2 attribute2) {
        InterpolatedFloat2 result;
        result.value = attribute0 * weights.x + attribute1 * weights.y + attribute2 * weights.z;
        result.ddx   = attribute0 * ddx.x + attribute1 * ddx.y + attribute2 * ddx.z;
        result.ddy   = attribute0 * ddy.x + attribute1 * ddy.y + attribute2 * ddy.z;
        return result;
    }

    /**
     * @param vertices vertices 0, 1, 2 of the triangle, in mesh space
     * @param modelViewProj mesh space to clip space transform
     * @param pixel pixel centre in pixels, as SV_Position.xy
     * @param screenSize render target size in pixels
     */
    public static TriangleBarycentrics fromVertices(Array<GPUVertex, 3> vertices, float4x4 modelViewProj, float2 pixel, float2 screenSize) {
        return TriangleBarycentrics(mul(modelViewProj, float4(vertices[0].position.xyz, 1.0)),
                                    mul(modelViewProj, float4(vertices[1].position.xyz, 1.0)),
                                    mul(modelViewProj, float4(vertices[2].position.xyz, 1.0)),
                                    pixel, screenSize);
    }
}
