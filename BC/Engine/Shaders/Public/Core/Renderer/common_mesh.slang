module common_mesh;

import GPUFrame;
import GPUDraw;
import GPUMesh;
import AtomicAdd;
import SphereVisibilityTest;
import HiZOCTest;

/// Threads per mesh-shader workgroup; one workgroup draws one meshlet.
public static const uint MESH_GROUP_SIZE = 64;

/**
 * Tests a meshlet against the frustum and, when frame->useHiZOC is set, the depth pyramid.
 * @param frame frame the meshlet is drawn in
 * @param depthPyramid depth pyramid built with frame->hiZViewProj
 * @param meshletSphere xyz = centre in world space, w = radius.
 * @return true when the meshlet is visible.
 */
public bool isMeshletVisible(GPUFrameData* frame, Sampler2D depthPyramid, float4 meshletSphere) {
    if (!sphereVisibleWorld(frame->camera.viewProj, meshletSphere)) return false;
    if (!frame->useHiZOC) return true;
    return !isOcludedByHiZ(depthPyramid, frame->hiZViewProj, meshletSphere);
}

/**
 * Adds one meshlet to frame->drawCounters as drawn or culled.
 * @param frame frame the meshlet is drawn in
 * @param meshlet meshlet that was tested
 * @param visible result of isMeshletVisible()
 * @note Called by one lane per workgroup.
 */
public void incrementMetricCounters(GPUFrameData* frame, GPUMeshlet meshlet, bool visible) {
    GPUDrawCounters* counters = frame->drawCounters;
    if (visible) {
        atomicAdd(&counters->meshletsDrawn, 1);
        atomicAdd(&counters->trianglesDrawn, meshlet.triangleCount);
    } else {
        atomicAdd(&counters->meshletsCulled, 1);
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
    // One triangle per uint: i0 | (i1 << 8) | (i2 << 16).
    const uint packed = mesh.meshletTriangles[meshlet.firstMeshletTriangle + i].packedTriangle;
    return uint3(packed & 0xffu, (packed >> 8) & 0xffu, (packed >> 16) & 0xffu);
}
