module;
#include <cassert>
#include "glm/glm.hpp"
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_precision.hpp>
export module Meshlet;

export import PositionGrid;

/**
 * Meshlet representation
 * firstMeshletVertex and firstMeshletTriangle index the owning mesh's arrays.
 * Vertices are owned by one meshlet each; their positions are offsets from base.
 * Bounding sphere center is an offset from base on the model's PositionGrid.
 ***/
export struct Meshlet {
    uint32_t firstMeshletVertex = 0;
    uint32_t firstMeshletTriangle = 0;
    GridPoint base{};                    // origin of the meshlet's vertex positions
    uint32_t sphereX : 10;               // grid steps from base
    uint32_t sphereY : 10;               // grid steps from base
    uint32_t sphereZ : 10;               // grid steps from base
    uint32_t reserved0 : 2;
    uint32_t sphereRadius : 11;          // grid steps
    uint32_t vertexCount : 8;
    uint32_t triangleCount : 8;
    uint32_t reserved1 : 5;
    uint32_t packed_normalCone{};        // snorm [8x 8y 8z 8cutoff]

    /**
     * Rounds outward: the stored sphere contains the sphere passed in.
     * @param center sphere center in mesh space
     * @param radius sphere radius in mesh space units
     * @param grid grid of the owning model
     * @note Meshlet::base must be set before this is called.
     * @note Radius must be within 2046 grid steps.
     */
    void packBoundingSphere(glm::vec3 center, float radius, const PositionGrid& grid) {
        GridPoint offset = glm::clamp(grid.quantize(center) - base, GridPoint(0), GridPoint(1023));
        float radiusSteps = glm::ceil((radius + glm::distance(center, grid.dequantize(base + offset))) / grid.step);
        assert(radiusSteps <= 2047.0f);
        sphereX = static_cast<uint32_t>(offset.x);
        sphereY = static_cast<uint32_t>(offset.y);
        sphereZ = static_cast<uint32_t>(offset.z);
        sphereRadius = static_cast<uint32_t>(radiusSteps);
    }
    /**
     * @param grid grid of the owning model
     * @return sphere center in mesh space in xyz, radius in mesh space units in w.
     */
    [[nodiscard]] glm::vec4 unpackBoundingSphere(const PositionGrid& grid) const {
        glm::vec3 center = grid.dequantize(base + GridPoint(sphereX, sphereY, sphereZ));
        return { center, static_cast<float>(sphereRadius) * grid.step };
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

static_assert(sizeof(Meshlet) == 32);
