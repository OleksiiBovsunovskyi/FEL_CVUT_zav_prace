module;
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <meshoptimizer.h>

module ClusterLODGenerator;

import Logger;

namespace {

/// Grid points stay below 2^MAX_GRID_POINT_BITS in magnitude, inside the exact integer range of float.
constexpr int MAX_GRID_POINT_BITS = 23;

/// Meshlets as built by meshoptimizer.
struct BuiltMeshlets {
    std::vector<meshopt_Meshlet> meshlets;
    /// Indexes the imported vertices.
    std::vector<uint32_t> vertices;
    /// Indexes each meshlet's own range of vertices.
    std::vector<uint8_t> triangles;

    [[nodiscard]] std::span<const uint32_t> verticesOf(const meshopt_Meshlet& meshlet) const {
        return {vertices.data() + meshlet.vertex_offset, meshlet.vertex_count};
    }
    [[nodiscard]] std::span<const uint8_t> trianglesOf(const meshopt_Meshlet& meshlet) const {
        return {triangles.data() + meshlet.triangle_offset, size_t{meshlet.triangle_count} * 3};
    }
};

BuiltMeshlets buildMeshlets(std::span<const ImportedVertex> vertices, std::span<const uint32_t> indices,
                            const ClusterLODSettings& settings) {
    BuiltMeshlets built;
    built.meshlets.resize(meshopt_buildMeshletsBound(indices.size(), settings.maxVerticesPerCluster,
                                                     settings.maxTrianglesPerCluster));
    built.vertices.resize(indices.size());
    built.triangles.resize(indices.size());
    built.meshlets.resize(meshopt_buildMeshlets(
        built.meshlets.data(), built.vertices.data(), built.triangles.data(), indices.data(), indices.size(),
        glm::value_ptr(vertices.front().position), vertices.size(), sizeof(ImportedVertex),
        settings.maxVerticesPerCluster, settings.maxTrianglesPerCluster, 0.5f));
    return built;
}

/// @return length of the meshlet's shortest edge longer than zero, infinity when it has none.
float shortestEdge(const BuiltMeshlets& built, const meshopt_Meshlet& meshlet,
                   std::span<const ImportedVertex> vertices) {
    const std::span<const uint32_t> local = built.verticesOf(meshlet);
    const std::span<const uint8_t> corners = built.trianglesOf(meshlet);
    float shortest = std::numeric_limits<float>::infinity();
    for (size_t t = 0; t < corners.size(); t += 3) {
        for (size_t edge = 0; edge < 3; ++edge) {
            const float length = glm::distance(vertices[local[corners[t + edge]]].position,
                                               vertices[local[corners[t + (edge + 1) % 3]]].position);
            if (length > 0.0f) shortest = std::min(shortest, length);
        }
    }
    return shortest;
}

/**
 * @param wantedSteps largest position step of each meshlet, infinity for no limit
 * @param vertices vertices of the primitive
 * @return grid with a power of two step no coarser than the finest finite wanted step,
 *         and fine enough to keep grid points below 2^MAX_GRID_POINT_BITS.
 */
PositionGrid chooseGrid(std::span<const float> wantedSteps, std::span<const ImportedVertex> vertices) {
    float largestCoordinate = 0.0f;
    for (const ImportedVertex& vertex : vertices) {
        const glm::vec3 magnitude = glm::abs(vertex.position);
        largestCoordinate = std::max({largestCoordinate, magnitude.x, magnitude.y, magnitude.z});
    }
    const float finest = *std::ranges::min_element(wantedSteps);

    int exponent = std::isfinite(finest) ? std::ilogb(finest) : 0;
    if (largestCoordinate > 0.0f)
        exponent = std::max(exponent, std::ilogb(largestCoordinate) + 1 - MAX_GRID_POINT_BITS);
    return PositionGrid{.step = std::ldexp(1.0f, exponent)};
}

/// @return smallest step exponent that keeps offsets across extent within MESHLET_MAX_POSITION_BITS bits.
uint32_t smallestFittingExponent(glm::vec3 extent, const PositionGrid& grid) {
    const double extentSteps = std::ceil(std::max({extent.x, extent.y, extent.z}) / grid.step) + 2.0;
    uint32_t exponent = 0;
    while (extentSteps / std::ldexp(1.0, static_cast<int>(exponent)) >= std::ldexp(1.0, MESHLET_MAX_POSITION_BITS))
        ++exponent;
    return exponent;
}

/**
 * Lowers step exponents until meshlets sharing a position differ by at most 1, never below lowest.
 * @param exponents step exponent of each meshlet
 * @param lowest smallest allowed step exponent of each meshlet
 * @param positionIds position id of each imported vertex
 */
void limitNeighbourExponents(std::span<uint32_t> exponents, std::span<const uint32_t> lowest,
                             const BuiltMeshlets& built, std::span<const uint32_t> positionIds) {
    std::vector<uint32_t> finestAtPosition(positionIds.size());
    bool changed = true;
    while (changed) {
        changed = false;
        std::ranges::fill(finestAtPosition, std::numeric_limits<uint32_t>::max());
        for (size_t m = 0; m < built.meshlets.size(); ++m)
            for (const uint32_t vertex : built.verticesOf(built.meshlets[m]))
                finestAtPosition[positionIds[vertex]] = std::min(finestAtPosition[positionIds[vertex]], exponents[m]);

        for (size_t m = 0; m < built.meshlets.size(); ++m) {
            uint32_t limit = exponents[m];
            for (const uint32_t vertex : built.verticesOf(built.meshlets[m]))
                limit = std::min(limit, finestAtPosition[positionIds[vertex]] + 1);
            const uint32_t limited = std::max(lowest[m], limit);
            if (limited != exponents[m]) {
                exponents[m] = limited;
                changed = true;
            }
        }
    }
}

/**
 * Snaps each position to the coarsest step of the meshlets sharing it.
 * @param exponents step exponent of each meshlet
 * @param positionIds position id of each imported vertex
 * @return grid point of each imported vertex.
 */
std::vector<GridPoint> snapPositions(std::span<const ImportedVertex> vertices, const BuiltMeshlets& built,
                                     std::span<const uint32_t> exponents, std::span<const uint32_t> positionIds,
                                     const PositionGrid& grid) {
    std::vector<uint32_t> coarsestAtPosition(vertices.size(), 0);
    for (size_t m = 0; m < built.meshlets.size(); ++m)
        for (const uint32_t vertex : built.verticesOf(built.meshlets[m]))
            coarsestAtPosition[positionIds[vertex]] = std::max(coarsestAtPosition[positionIds[vertex]], exponents[m]);

    std::vector<GridPoint> points(vertices.size());
    for (size_t vertex = 0; vertex < vertices.size(); ++vertex) {
        const int32_t exponent = static_cast<int32_t>(coarsestAtPosition[positionIds[vertex]]);
        const float snapStep = grid.step * static_cast<float>(1u << exponent);
        points[vertex] = GridPoint(glm::round(vertices[positionIds[vertex]].position / snapStep)) << exponent;
    }
    return points;
}

/**
 * Builds meshlets with meshoptimizer and packs them onto one PositionGrid.
 * Each meshlet's step is 2^n grid steps, at most settings.positionStepPerEdge of its shortest edge.
 * Meshlets sharing a position differ by at most one step exponent, unless an offset would not fit otherwise.
 * @param outputPositionIds replaced with the position id of each output vertex
 */
bool buildPackedMeshlets(std::span<const ImportedVertex> vertices,
                         std::span<const uint32_t> indices,
                         const ClusterLODSettings& settings,
                         GeneratedClusterLOD& output,
                         std::vector<uint32_t>& outputPositionIds) {
    const BuiltMeshlets built = buildMeshlets(vertices, indices, settings);
    const size_t meshletCount = built.meshlets.size();

    std::vector<uint32_t> positionIds(vertices.size());
    meshopt_generatePositionRemap(positionIds.data(), glm::value_ptr(vertices.front().position), vertices.size(),
                                  sizeof(ImportedVertex));

    std::vector<float> wantedSteps(meshletCount);
    for (size_t m = 0; m < meshletCount; ++m)
        wantedSteps[m] = shortestEdge(built, built.meshlets[m], vertices) * settings.positionStepPerEdge;
    output.grid = chooseGrid(wantedSteps, vertices);

    std::vector<uint32_t> exponents(meshletCount);
    std::vector<uint32_t> lowest(meshletCount);
    for (size_t m = 0; m < meshletCount; ++m) {
        glm::vec3 low{std::numeric_limits<float>::max()};
        glm::vec3 high{std::numeric_limits<float>::lowest()};
        for (const uint32_t vertex : built.verticesOf(built.meshlets[m])) {
            low  = glm::min(low, vertices[vertex].position);
            high = glm::max(high, vertices[vertex].position);
        }
        lowest[m] = smallestFittingExponent(high - low, output.grid);
        if (lowest[m] > MESHLET_MAX_STEP_EXPONENT) {
            logError("ClusterLOD: meshlet " + std::to_string(m) + " is too large for " +
                     std::to_string(MESHLET_MAX_POSITION_BITS) + "-bit position offsets");
            return false;
        }
        const int wanted = std::isfinite(wantedSteps[m])
            ? std::ilogb(wantedSteps[m]) - std::ilogb(output.grid.step)
            : static_cast<int>(MESHLET_MAX_STEP_EXPONENT);
        exponents[m] = std::max(lowest[m], static_cast<uint32_t>(
            std::clamp(wanted, 0, static_cast<int>(MESHLET_MAX_STEP_EXPONENT))));
    }
    limitNeighbourExponents(exponents, lowest, built, positionIds);

    const std::vector<GridPoint> points = snapPositions(vertices, built, exponents, positionIds, output.grid);
    std::vector<glm::vec3> snappedPositions(vertices.size());
    for (size_t vertex = 0; vertex < vertices.size(); ++vertex)
        snappedPositions[vertex] = output.grid.dequantize(points[vertex]);

    std::vector<GridPoint> meshletPoints;
    for (size_t m = 0; m < meshletCount; ++m) {
        const meshopt_Meshlet& meshlet = built.meshlets[m];
        const std::span<const uint32_t> localVertices = built.verticesOf(meshlet);
        const std::span<const uint8_t> localTriangles = built.trianglesOf(meshlet);

        Meshlet packed{};
        packed.firstMeshletVertex   = static_cast<uint32_t>(output.vertices.size());
        packed.firstMeshletTriangle = static_cast<uint32_t>(output.triangles.size());
        packed.vertexCount          = meshlet.vertex_count;
        packed.triangleCount        = meshlet.triangle_count;
        packed.stepExponent         = exponents[m];

        meshletPoints.clear();
        for (const uint32_t vertex : localVertices) meshletPoints.push_back(points[vertex]);
        if (!packed.packPositions(meshletPoints, output.positions)) {
            logError("ClusterLOD: meshlet " + std::to_string(m) + " has a position offset wider than " +
                     std::to_string(MESHLET_MAX_POSITION_BITS) + " bits");
            return false;
        }

        const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            localVertices.data(), localTriangles.data(), meshlet.triangle_count,
            glm::value_ptr(snappedPositions.front()), snappedPositions.size(), sizeof(glm::vec3));
        glm::vec3 center(bounds.center[0], bounds.center[1], bounds.center[2]);
        float radius = bounds.radius;
        if (radius == 0.0f) {
            // meshopt_computeMeshletBounds returns zero bounds when every triangle is degenerate.
            glm::vec3 lowPosition = output.grid.dequantize(meshletPoints.front());
            glm::vec3 highPosition = lowPosition;
            for (const GridPoint& point : meshletPoints) {
                lowPosition = glm::min(lowPosition, output.grid.dequantize(point));
                highPosition = glm::max(highPosition, output.grid.dequantize(point));
            }
            center = (lowPosition + highPosition) * 0.5f;
            radius = glm::distance(center, highPosition);
        }
        if (!packed.packBoundingSphere(center, radius, output.grid)) {
            logError("ClusterLOD: meshlet " + std::to_string(m) + " has an invalid bounding sphere");
            return false;
        }
        packed.packNormalCone(glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]),
                              bounds.cone_cutoff);
        output.meshlets.push_back(packed);

        for (const uint32_t index : localVertices) {
            const ImportedVertex& source = vertices[index];
            Vertex vertex{};
            vertex.packNormalTangent(source.normal, glm::vec3(source.tangent), source.tangent.w);
            vertex.packTexCoord(source.texCoord);
            output.vertices.push_back(vertex);
            outputPositionIds.push_back(positionIds[index]);
        }

        for (size_t t = 0; t < localTriangles.size(); t += 3)
            output.triangles.push_back(Triangle{
                localTriangles[t + 0], localTriangles[t + 1], localTriangles[t + 2], 0});
    }
    output.positions.push_back(0);

    return !output.empty();
}

/**
 * Checks the invariants the mesh shader and the frustum cull rely on.
 * @param positionIds position id of each output vertex
 * @return false on the first violation, after logging which one.
 */
bool validate(const GeneratedClusterLOD& output, std::span<const uint32_t> positionIds) {
    std::unordered_map<uint32_t, GridPoint> pointOfPosition;
    for (size_t m = 0; m < output.meshlets.size(); ++m) {
        const Meshlet& meshlet = output.meshlets[m];

        if (size_t{meshlet.firstPositionWord} + meshlet.positionWordCount() >= output.positions.size()) {
            logError("ClusterLOD: meshlet " + std::to_string(m) +
                     " position words reach the last word of the position stream");
            return false;
        }

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

        for (uint32_t i = 0; i < meshlet.vertexCount; ++i) {
            const GridPoint point = meshlet.unpackGridPoint(output.positions, i);
            const auto [stored, inserted] =
                pointOfPosition.try_emplace(positionIds[meshlet.firstMeshletVertex + i], point);
            if (!inserted && stored->second != point) {
                logError("ClusterLOD: meshlet " + std::to_string(m) + " vertex " + std::to_string(i) +
                         " decodes to a different grid point than the same position in another meshlet");
                return false;
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
        settings.maxTrianglesPerCluster > MESHLET_MAX_TRIANGLES ||
        !(settings.positionStepPerEdge > 0.0f) || !std::isfinite(settings.positionStepPerEdge)) {
        logError("ClusterLOD: bad input, cluster limits outside 1.." +
                 std::to_string(MESHLET_MAX_VERTICES) + "/4.." +
                 std::to_string(MESHLET_MAX_TRIANGLES) + ", or a position step per edge that is not positive");
        return false;
    }

    for (uint32_t index : indices)
        if (index >= vertices.size())
            return false;

    std::vector<uint32_t> positionIds;
    return buildPackedMeshlets(vertices, indices, settings, output, positionIds) && validate(output, positionIds);
}
