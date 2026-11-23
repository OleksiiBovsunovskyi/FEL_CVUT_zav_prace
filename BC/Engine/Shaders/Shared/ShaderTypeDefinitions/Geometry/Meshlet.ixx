module;
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include "glm/glm.hpp"
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_precision.hpp>
export module Meshlet;

export import PositionGrid;
import BitLib;

/**
 * 32 bits of a mesh's position stream.
 * Word is just storage/read format, vertices can take multiple words, or sit between their boundaries
 */
export using PositionWord = uint32_t;

/// Bits of Meshlet::positionBitsMinus1X, positionBitsMinus1Y and positionBitsMinus1Z.
constexpr uint32_t MESHLET_POSITION_WIDTH_BITS = 4;
/// Bits of Meshlet::stepExponent.
constexpr uint32_t MESHLET_STEP_EXPONENT_BITS = 4;
/// Bits of Meshlet::vertexCount and Meshlet::triangleCount.
constexpr uint32_t MESHLET_COUNT_BITS = 8;
/**
 * Bits of Meshlet::sphereX, sphereY and sphereZ.
 * Widest position offset the sphere center fields hold without coarsening the sphere unit.
 */
constexpr uint32_t MESHLET_SPHERE_OFFSET_BITS = 10;
/// Bits of Meshlet::sphereRadius.
constexpr uint32_t MESHLET_SPHERE_RADIUS_BITS = 11;

/// Widest position offset on one axis, in bits.
export constexpr uint32_t MESHLET_MAX_POSITION_BITS = 1u << MESHLET_POSITION_WIDTH_BITS;
/// Largest Meshlet::stepExponent.
export constexpr uint32_t MESHLET_MAX_STEP_EXPONENT = (1u << MESHLET_STEP_EXPONENT_BITS) - 1;
/// Largest Meshlet::vertexCount and Meshlet::triangleCount.
export constexpr uint32_t MESHLET_MAX_COUNT = (1u << MESHLET_COUNT_BITS) - 1;
/// Largest bounding sphere center offset on one axis, in sphere units.
constexpr int32_t MESHLET_MAX_SPHERE_OFFSET = (1 << MESHLET_SPHERE_OFFSET_BITS) - 1;
/// Largest bounding sphere radius, in sphere units.
constexpr uint32_t MESHLET_MAX_SPHERE_RADIUS = (1u << MESHLET_SPHERE_RADIUS_BITS) - 1;

/**
 * Meshlet representation
 * firstMeshletVertex and firstMeshletTriangle index the owning mesh's arrays.
 * firstPositionWord indexes the owning mesh's position stream.
 * Vertices are owned by one meshlet each.
 * Meshlet step is 2^stepExponent model grid steps.
 * Vertex i is at grid point base + offset * 2^stepExponent.
 * Its offset is read from the position stream at bit i * positionStrideBits() from firstPositionWord, x then y then z.
 * Bounding sphere is in units of 2^sphereShift() grid steps; its center is an offset from base.
 ***/
export struct Meshlet {
    uint32_t firstMeshletVertex = 0;
    uint32_t firstMeshletTriangle = 0;
    uint32_t firstPositionWord = 0;
    GridPoint base{};                    // grid point of offset 0, multiple of 2^stepExponent
    uint32_t sphereX : MESHLET_SPHERE_OFFSET_BITS;              // sphere units from base
    uint32_t sphereY : MESHLET_SPHERE_OFFSET_BITS;              // sphere units from base
    uint32_t sphereZ : MESHLET_SPHERE_OFFSET_BITS;              // sphere units from base
    uint32_t reserved0 : 2;
    uint32_t sphereRadius : MESHLET_SPHERE_RADIUS_BITS;         // sphere units
    uint32_t vertexCount : MESHLET_COUNT_BITS;
    uint32_t triangleCount : MESHLET_COUNT_BITS;
    uint32_t reserved1 : 5;
    uint32_t packed_normalCone{};                               // snorm [8x 8y 8z 8cutoff]
    uint32_t positionBitsMinus1X : MESHLET_POSITION_WIDTH_BITS; // offset bits on x, minus 1
    uint32_t positionBitsMinus1Y : MESHLET_POSITION_WIDTH_BITS; // offset bits on y, minus 1
    uint32_t positionBitsMinus1Z : MESHLET_POSITION_WIDTH_BITS; // offset bits on z, minus 1
    uint32_t stepExponent : MESHLET_STEP_EXPONENT_BITS;
    uint32_t reserved2 : 16;

    /// @return offset bits on x, y and z, 1..MESHLET_MAX_POSITION_BITS each.
    [[nodiscard]] glm::uvec3 positionBits() const {
        return glm::uvec3(positionBitsMinus1X, positionBitsMinus1Y, positionBitsMinus1Z) + 1u;
    }
    /// @return bits of one vertex position in the position stream.
    [[nodiscard]] uint32_t positionStrideBits() const {
        const glm::uvec3 bits = positionBits();
        return bits.x + bits.y + bits.z;
    }
    /// @return words of the position stream holding this meshlet's positions.
    [[nodiscard]] uint32_t positionWordCount() const {
        return (vertexCount * positionStrideBits() + 31) / 32;
    }

    /**
     * Sets base and the offset bit widths, then appends the positions to the stream.
     * @param points grid points of the meshlet's vertices, in vertex order
     * @param positions position stream of the owning mesh
     * @return false when an offset needs more than MESHLET_MAX_POSITION_BITS bits; positions is unchanged then.
     * @note vertexCount and stepExponent must be set, and every point must be a multiple of 2^stepExponent.
     */
    [[nodiscard]] bool packPositions(std::span<const GridPoint> points, std::vector<PositionWord>& positions) {
        assert(points.size() == vertexCount);
        GridPoint low{std::numeric_limits<int32_t>::max()};
        GridPoint high{std::numeric_limits<int32_t>::min()};
        for (const GridPoint point : points) {
            assert(glm::all(glm::equal(point & GridPoint((1 << stepExponent) - 1), GridPoint(0))));
            low  = glm::min(low, point);
            high = glm::max(high, point);
        }
        const glm::uvec3 largestOffset((high - low) >> static_cast<int32_t>(stepExponent));
        glm::uvec3 bits;
        for (int axis = 0; axis < 3; ++axis)
            bits[axis] = std::max(1u, static_cast<uint32_t>(std::bit_width(largestOffset[axis])));
        if (glm::any(glm::greaterThan(bits, glm::uvec3(MESHLET_MAX_POSITION_BITS)))) return false;

        base = low;
        positionBitsMinus1X = bits.x - 1;
        positionBitsMinus1Y = bits.y - 1;
        positionBitsMinus1Z = bits.z - 1;
        firstPositionWord = static_cast<uint32_t>(positions.size());
        positions.resize(positions.size() + positionWordCount(), 0);

        uint32_t bitOffset = 0;
        for (const GridPoint point : points) {
            const glm::uvec3 offset((point - base) >> static_cast<int32_t>(stepExponent));
            for (int axis = 0; axis < 3; ++axis) {
                writeBits(positions, firstPositionWord, bitOffset, bits[axis], offset[axis]);
                bitOffset += bits[axis];
            }
        }
        return true;
    }
    /**
     * @param positions position stream of the owning mesh
     * @param i vertex within the meshlet, 0..vertexCount-1
     * @return grid point of vertex i.
     * @note The stream must hold one word after this meshlet's words.
     */
    [[nodiscard]] GridPoint unpackGridPoint(std::span<const PositionWord> positions, uint32_t i) const {
        const glm::uvec3 bits = positionBits();
        const uint32_t bitOffset = i * positionStrideBits();
        const glm::uvec3 offset(readBits(positions, firstPositionWord, bitOffset, bits.x),
                                readBits(positions, firstPositionWord, bitOffset + bits.x, bits.y),
                                readBits(positions, firstPositionWord, bitOffset + bits.x + bits.y, bits.z));
        return base + (GridPoint(offset) << static_cast<int32_t>(stepExponent));
    }
    /**
     * @param positions position stream of the owning mesh
     * @param grid grid of the owning model
     * @param i vertex within the meshlet, 0..vertexCount-1
     * @return mesh space position of vertex i.
     * @note The stream must hold one word after this meshlet's words.
     */
    [[nodiscard]] glm::vec3 unpackPosition(std::span<const PositionWord> positions, const PositionGrid& grid,
                                           uint32_t i) const {
        return grid.dequantize(unpackGridPoint(positions, i));
    }

    /// @return log2 of the bounding sphere unit, in grid steps.
    [[nodiscard]] uint32_t sphereShift() const {
        const glm::uvec3 bits = positionBits();
        const uint32_t widest = std::max({bits.x, bits.y, bits.z});
        return stepExponent + (widest > MESHLET_SPHERE_OFFSET_BITS ? widest - MESHLET_SPHERE_OFFSET_BITS : 0);
    }

    /**
     * Rounds outward: the stored sphere contains the sphere passed in.
     * @param center sphere center in mesh space
     * @param radius sphere radius in mesh space units
     * @param grid grid of the owning model
     * @return false, leaving the sphere fields unchanged, when center or radius is not finite
     *         or the radius does not fit sphereRadius.
     * @note packPositions must be called before this.
     */
    [[nodiscard]] bool packBoundingSphere(glm::vec3 center, float radius, const PositionGrid& grid) {
        if (glm::any(glm::isnan(center) || glm::isinf(center)) || !std::isfinite(radius)) return false;
        const auto shift = static_cast<int32_t>(sphereShift());
        const float unit = grid.step * static_cast<float>(1u << shift);
        const GridPoint offset = glm::clamp(GridPoint(glm::round((center - grid.dequantize(base)) / unit)),
                                            GridPoint(0), GridPoint(MESHLET_MAX_SPHERE_OFFSET));
        const glm::vec3 storedCenter = grid.dequantize(base + (offset << shift));
        const float radiusUnits = glm::ceil((radius + glm::distance(center, storedCenter)) / unit);
        if (radiusUnits > static_cast<float>(MESHLET_MAX_SPHERE_RADIUS)) return false;
        sphereX = static_cast<uint32_t>(offset.x);
        sphereY = static_cast<uint32_t>(offset.y);
        sphereZ = static_cast<uint32_t>(offset.z);
        sphereRadius = static_cast<uint32_t>(radiusUnits);
        return true;
    }
    /**
     * @param grid grid of the owning model
     * @return sphere center in mesh space in xyz, radius in mesh space units in w.
     */
    [[nodiscard]] glm::vec4 unpackBoundingSphere(const PositionGrid& grid) const {
        const int32_t shift = static_cast<int32_t>(sphereShift());
        const glm::vec3 center = grid.dequantize(base + (GridPoint(sphereX, sphereY, sphereZ) << shift));
        return { center, static_cast<float>(sphereRadius) * grid.step * static_cast<float>(1u << shift) };
    }

    /**
     * Rounds the cutoff up
     * @param axis unit cone axis
     * @param cutoff cosine cutoff
     */
    void packNormalCone(glm::vec3 axis, float cutoff) {
        glm::vec3 storedAxis = glm::vec3(glm::unpackSnorm4x8(glm::packSnorm4x8(glm::vec4(axis, 0.0f))));
        glm::vec3 axisError = glm::abs(storedAxis - axis);
        float storedCutoff = cutoff + axisError.x + axisError.y + axisError.z + 1.0f / 127.0f;
        packed_normalCone = glm::packSnorm4x8(glm::vec4(axis, glm::min(storedCutoff, 1.0f)));
    }
    /// @return cone axis in xyz, length is within rounding of 1; cosine cutoff in w.
    [[nodiscard]] glm::vec4 unpackNormalCone() const {
        return glm::unpackSnorm4x8(packed_normalCone);
    }
};

static_assert(sizeof(Meshlet) == 40);
static_assert(MESHLET_MAX_POSITION_BITS <= 32, "readPositionBits reads at most 32 bits");
