module;
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include <meshoptimizer.h>
#define CLUSTERLOD_IMPLEMENTATION
#include <clusterlod.h>

module ClusterLODGenerator;

import Logger;

namespace {

void appendMeshlet(GeneratedClusterLOD& output,
                   std::span<const uint32_t> localVertices,
                   std::span<const uint8_t> localTriangles,
                   uint32_t triangleCount,
                   const float center[3], float radius,
                   const meshopt_Bounds& cone,
                   int32_t group, int32_t refinedGroup) {
    GPUMeshlet meshlet{};
    meshlet.vertexOffset =
        static_cast<uint32_t>(output.meshletVertexIndices.size());
    meshlet.triangleOffset =
        static_cast<uint32_t>(output.meshletTriangles.size());
    meshlet.vertexCount = static_cast<uint32_t>(localVertices.size());
    meshlet.triangleCount = triangleCount;
    meshlet.boundingSphere = glm::vec4(center[0], center[1], center[2], radius);
    meshlet.normalCone = glm::vec4(cone.cone_axis[0], cone.cone_axis[1],
                                   cone.cone_axis[2], cone.cone_cutoff);

    const uint32_t meshletIndex =
        static_cast<uint32_t>(output.meshlets.size());
    output.meshlets.push_back(meshlet);
    output.meshletVertexIndices.insert(
        output.meshletVertexIndices.end(),
        localVertices.begin(), localVertices.end());

    for (uint32_t triangle = 0; triangle < triangleCount; ++triangle) {
        const size_t base = size_t(triangle) * 3;
        output.meshletTriangles.push_back(
            uint32_t(localTriangles[base + 0]) |
            (uint32_t(localTriangles[base + 1]) << 8) |
            (uint32_t(localTriangles[base + 2]) << 16));
    }

    output.clusters.push_back({
        meshletIndex,
        group,
        refinedGroup,
        0,
    });
}

bool generateSingleLevel(std::span<const GPUVertex> vertices,
                         std::span<const uint32_t> indices,
                         const ClusterLODSettings& settings,
                         GeneratedClusterLOD& output) {
    const size_t maxMeshlets = meshopt_buildMeshletsBound(
        indices.size(), settings.maxVerticesPerCluster,
        settings.maxTrianglesPerCluster);

    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    std::vector<uint32_t> meshletVertices(indices.size());
    std::vector<uint8_t> meshletTriangles(indices.size());

    const size_t meshletCount = meshopt_buildMeshlets(
        meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
        indices.data(), indices.size(), glm::value_ptr(vertices.front().position),
        vertices.size(), sizeof(GPUVertex),
        settings.maxVerticesPerCluster,
        settings.maxTrianglesPerCluster,
        0.5f);
    meshlets.resize(meshletCount);

    /**
     * Not computeClusterBounds: that is specified for a single cluster and
     * asserts past 512 triangles or 256 unique vertices. This is the whole mesh.
     */
    const meshopt_Bounds meshBounds = meshopt_computeSphereBounds(
        glm::value_ptr(vertices.front().position), vertices.size(), sizeof(GPUVertex),
        nullptr, 0);

    GPUClusterGroup group{};
    group.firstCluster = 0;
    group.clusterCount = static_cast<uint32_t>(meshletCount);
    group.depth = 0;
    group.boundingSphere = glm::vec4(meshBounds.center[0], meshBounds.center[1],
                                     meshBounds.center[2], meshBounds.radius);
    group.error = FLT_MAX;
    output.groups.push_back(group);

    for (const meshopt_Meshlet& meshlet : meshlets) {
        const std::span<const uint32_t> localVertices{
            meshletVertices.data() + meshlet.vertex_offset,
            meshlet.vertex_count,
        };
        const std::span<const uint8_t> localTriangles{
            meshletTriangles.data() + meshlet.triangle_offset,
            meshlet.triangle_count * 3,
        };
        const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            localVertices.data(), localTriangles.data(),
            meshlet.triangle_count, glm::value_ptr(vertices.front().position),
            vertices.size(), sizeof(GPUVertex));

        appendMeshlet(output, localVertices, localTriangles,
                      meshlet.triangle_count, bounds.center, bounds.radius,
                      bounds, 0, -1);
    }

    return !output.empty();
}

bool generateHierarchy(std::span<const GPUVertex> vertices,
                       std::span<const uint32_t> indices,
                       const ClusterLODSettings& settings,
                       GeneratedClusterLOD& output) {
    clodConfig config =
        clodDefaultConfig(settings.maxTrianglesPerCluster);
    config.max_vertices       = settings.maxVerticesPerCluster;
    config.simplify_ratio     = settings.simplifyRatio;
    config.simplify_threshold = settings.simplifyThreshold;

    /* normal.xyzw, tangent.xyzw, texCoord.xy */
    const float attributeWeights[10] = {
        1.0f, 1.0f, 1.0f, 0.0f,
        0.25f, 0.25f, 0.25f, 0.25f,
        1.0f, 1.0f,
    };

    clodMesh source{};
    source.indices = indices.data();
    source.index_count = indices.size();
    source.vertex_count = vertices.size();
    source.vertex_positions = glm::value_ptr(vertices.front().position);
    source.vertex_positions_stride = sizeof(GPUVertex);
    source.vertex_attributes = glm::value_ptr(vertices.front().normal);
    source.vertex_attributes_stride = sizeof(GPUVertex);
    source.attribute_weights = attributeWeights;
    source.attribute_count = 10;
    source.attribute_protect_mask = (1u << 8) | (1u << 9);

    clodBuild(config, source,
        [&](clodGroup group, const clodCluster* generated,
            size_t clusterCount) -> int {
            const int groupIndex = static_cast<int>(output.groups.size());

            GPUClusterGroup gpuGroup{};
            gpuGroup.firstCluster =
                static_cast<uint32_t>(output.clusters.size());
            gpuGroup.clusterCount = static_cast<uint32_t>(clusterCount);
            gpuGroup.depth = static_cast<uint32_t>(group.depth);
            gpuGroup.boundingSphere = glm::vec4(
                group.simplified.center[0], group.simplified.center[1],
                group.simplified.center[2], group.simplified.radius);
            gpuGroup.error = group.simplified.error;
            output.groups.push_back(gpuGroup);

            for (size_t i = 0; i < clusterCount; ++i) {
                const clodCluster& cluster = generated[i];

                std::vector<uint32_t> localVertices(cluster.vertex_count);
                std::vector<uint8_t> localTriangles(cluster.index_count);
                localVertices.resize(clodLocalIndices(
                    localVertices.data(), localTriangles.data(),
                    cluster.indices, cluster.index_count));

                const meshopt_Bounds bounds = meshopt_computeClusterBounds(
                    cluster.indices, cluster.index_count,
                    glm::value_ptr(vertices.front().position), vertices.size(),
                    sizeof(GPUVertex));

                appendMeshlet(
                    output, localVertices, localTriangles,
                    static_cast<uint32_t>(cluster.index_count / 3),
                    cluster.bounds.center, cluster.bounds.radius, bounds,
                    groupIndex, cluster.refined);
            }

            return groupIndex;
        });

    return !output.empty();
}

/**
 * Checks the invariants the mesh shader and the frustum cull rely on. Every one
 * of these produces missing or wrong geometry if violated, with no error
 * anywhere else.
 *
 * One linear pass over data that was just built, at load time.
 *
 * @return false on the first violation, after logging which one.
 */
bool validate(const GeneratedClusterLOD& output, size_t vertexCount) {
    for (size_t m = 0; m < output.meshlets.size(); ++m) {
        const GPUMeshlet& meshlet = output.meshlets[m];

        const glm::vec4& sphere = meshlet.boundingSphere;
        // Negated so a NaN from a degenerate cluster fails too.
        if (!(sphere.w > 0.0f) || !std::isfinite(sphere.x) ||
            !std::isfinite(sphere.y) || !std::isfinite(sphere.z)) {
            logError("ClusterLOD: meshlet " + std::to_string(m) +
                     " has a bounding sphere with a non-positive radius or a "
                     "non-finite centre");
            return false;
        }

        if (meshlet.vertexOffset + meshlet.vertexCount >
                output.meshletVertexIndices.size() ||
            meshlet.triangleOffset + meshlet.triangleCount >
                output.meshletTriangles.size()) {
            logError("ClusterLOD: meshlet " + std::to_string(m) +
                     " ranges outside the index arrays");
            return false;
        }

        for (uint32_t i = 0; i < meshlet.vertexCount; ++i) {
            if (output.meshletVertexIndices[meshlet.vertexOffset + i] >= vertexCount) {
                logError("ClusterLOD: meshlet " + std::to_string(m) +
                         " references a vertex past the end");
                return false;
            }
        }

        for (uint32_t t = 0; t < meshlet.triangleCount; ++t) {
            const uint32_t packed =
                output.meshletTriangles[meshlet.triangleOffset + t];
            for (uint32_t shift : {0u, 8u, 16u}) {
                if (((packed >> shift) & 0xffu) >= meshlet.vertexCount) {
                    logError("ClusterLOD: meshlet " + std::to_string(m) +
                             " triangle " + std::to_string(t) +
                             " indexes past its own vertex count");
                    return false;
                }
            }
        }
    }

    for (const GPUCluster& cluster : output.clusters) {
        if (cluster.meshletIndex >= output.meshlets.size()) {
            logError("ClusterLOD: cluster references a missing meshlet");
            return false;
        }
        if (cluster.group < 0 ||
            static_cast<size_t>(cluster.group) >= output.groups.size()) {
            logError("ClusterLOD: cluster references a missing group");
            return false;
        }
    }

    for (const GPUClusterGroup& group : output.groups) {
        if (static_cast<size_t>(group.firstCluster) + group.clusterCount >
            output.clusters.size()) {
            logError("ClusterLOD: group ranges outside the cluster array");
            return false;
        }
    }

    return true;
}

} // namespace

bool generateClusterLOD(std::span<const GPUVertex> vertices,
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

    const bool built = settings.enabled
        ? generateHierarchy(vertices, indices, settings, output)
        : generateSingleLevel(vertices, indices, settings, output);

    return built && validate(output, vertices.size());
}
