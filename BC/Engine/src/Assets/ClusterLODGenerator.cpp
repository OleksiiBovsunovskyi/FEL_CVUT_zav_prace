module;
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <meshoptimizer.h>

module ClusterLODGenerator;

import Logger;

namespace {

/// Grid steps the largest meshlet extent is divided into; one step below the 0..1023 offset range of Vertex.
constexpr float MESHLET_EXTENT_STEPS = 1022.0f;

/**
 * Builds meshlets with meshoptimizer and packs them onto one PositionGrid.
 * Grid origin is the minimum corner of the mesh.
 * Grid step is the largest meshlet extent on any axis over MESHLET_EXTENT_STEPS.
 */
bool buildPackedMeshlets(std::span<const ImportedVertex> vertices,
                         std::span<const uint32_t> indices,
                         const ClusterLODSettings& settings,
                         GeneratedClusterLOD& output) {
    const size_t maxMeshlets = meshopt_buildMeshletsBound(
        indices.size(), settings.maxVerticesPerCluster,
        settings.maxTrianglesPerCluster);

    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    std::vector<uint32_t> meshletVertices(indices.size());
    std::vector<uint8_t> meshletTriangles(indices.size());

    const float* positions = glm::value_ptr(vertices.front().position);

    meshlets.resize(meshopt_buildMeshlets(
        meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
        indices.data(), indices.size(), positions,
        vertices.size(), sizeof(ImportedVertex),
        settings.maxVerticesPerCluster,
        settings.maxTrianglesPerCluster,
        0.5f));

    glm::vec3 meshMin{FLT_MAX};
    float largestExtent = 0.0f;
    for (const meshopt_Meshlet& meshlet : meshlets) {
        glm::vec3 low{FLT_MAX};
        glm::vec3 high{-FLT_MAX};
        for (uint32_t i = 0; i < meshlet.vertex_count; ++i) {
            const glm::vec3 position =
                vertices[meshletVertices[meshlet.vertex_offset + i]].position;
            low  = glm::min(low, position);
            high = glm::max(high, position);
        }
        const glm::vec3 extent = high - low;
        meshMin       = glm::min(meshMin, low);
        largestExtent = std::max({largestExtent, extent.x, extent.y, extent.z});
    }

    output.grid.origin = meshMin;
    output.grid.step =
        largestExtent > 0.0f ? largestExtent / MESHLET_EXTENT_STEPS : 1.0f;

    for (const meshopt_Meshlet& meshlet : meshlets) {
        const std::span<const uint32_t> localVertices{
            meshletVertices.data() + meshlet.vertex_offset,
            meshlet.vertex_count,
        };
        const std::span<const uint8_t> localTriangles{
            meshletTriangles.data() + meshlet.triangle_offset,
            size_t{meshlet.triangle_count} * 3,
        };
        const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            localVertices.data(), localTriangles.data(),
            meshlet.triangle_count, positions,
            vertices.size(), sizeof(ImportedVertex));

        GridPoint base{std::numeric_limits<int>::max()};
        for (uint32_t index : localVertices)
            base = glm::min(base, output.grid.quantize(vertices[index].position));

        Meshlet packed{};
        packed.firstMeshletVertex   = static_cast<uint32_t>(output.vertices.size());
        packed.firstMeshletTriangle = static_cast<uint32_t>(output.triangles.size());
        packed.base                 = base;
        packed.vertexCount          = meshlet.vertex_count;
        packed.triangleCount        = meshlet.triangle_count;
        packed.packBoundingSphere(
            glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]),
            bounds.radius, output.grid);
        packed.packNormalCone(
            glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]),
            bounds.cone_cutoff);
        output.meshlets.push_back(packed);

        for (uint32_t index : localVertices) {
            const ImportedVertex& source = vertices[index];
            Vertex vertex{};
            vertex.packPosition(source.position, output.grid, base);
            vertex.packNormalTangent(source.normal, glm::vec3(source.tangent),
                                     source.tangent.w);
            vertex.packTexCoord(source.texCoord);
            output.vertices.push_back(vertex);
        }

        for (size_t t = 0; t < localTriangles.size(); t += 3)
            output.triangles.push_back(Triangle{
                localTriangles[t + 0], localTriangles[t + 1], localTriangles[t + 2], 0});
    }

    return !output.empty();
}

/**
 * Checks the invariants the mesh shader and the frustum cull rely on.
 *
 * @return false on the first violation, after logging which one.
 */
bool validate(const GeneratedClusterLOD& output) {
    for (size_t m = 0; m < output.meshlets.size(); ++m) {
        const Meshlet& meshlet = output.meshlets[m];

        const glm::vec4 sphere = meshlet.unpackBoundingSphere(output.grid);
        // Negated: a NaN fails too.
        if (!(sphere.w > 0.0f) || !std::isfinite(sphere.x) ||
            !std::isfinite(sphere.y) || !std::isfinite(sphere.z)) {
            logError("ClusterLOD: meshlet " + std::to_string(m) +
                     " has a bounding sphere with a non-positive radius or a "
                     "non-finite centre");
            return false;
        }

        if (size_t{meshlet.firstMeshletVertex} + meshlet.vertexCount >
                output.vertices.size() ||
            size_t{meshlet.firstMeshletTriangle} + meshlet.triangleCount >
                output.triangles.size()) {
            logError("ClusterLOD: meshlet " + std::to_string(m) +
                     " ranges outside the vertex or triangle array");
            return false;
        }

        for (uint32_t t = 0; t < meshlet.triangleCount; ++t) {
            const Triangle& triangle =
                output.triangles[meshlet.firstMeshletTriangle + t];
            for (uint8_t corner : {triangle.v0, triangle.v1, triangle.v2}) {
                if (corner >= meshlet.vertexCount) {
                    logError("ClusterLOD: meshlet " + std::to_string(m) +
                             " triangle " + std::to_string(t) +
                             " indexes past its own vertex count");
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace

bool generateClusterLOD(std::span<const ImportedVertex> vertices,
                        std::span<const uint32_t> indices,
                        const ClusterLODSettings& settings,
                        GeneratedClusterLOD& output) {
    output = {};

    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0 ||
        settings.maxVerticesPerCluster == 0 ||
        settings.maxVerticesPerCluster > MESHLET_MAX_VERTICES ||
        settings.maxTrianglesPerCluster < 4 ||
        settings.maxTrianglesPerCluster > MESHLET_MAX_TRIANGLES) {
        logError("ClusterLOD: bad input or cluster limits outside 1.." +
                 std::to_string(MESHLET_MAX_VERTICES) + "/4.." +
                 std::to_string(MESHLET_MAX_TRIANGLES));
        return false;
    }

    for (uint32_t index : indices)
        if (index >= vertices.size())
            return false;

    return buildPackedMeshlets(vertices, indices, settings, output) && validate(output);
}
