module;
#include "glm/glm.hpp"
export module PositionGrid;

/// Point on a PositionGrid, in grid steps from its origin.
export using GridPoint = glm::ivec3;

/**
 * Integer grid the mesh space positions of one model are quantized to.
 * Position is origin + point * step.
 ***/
export struct PositionGrid {
    glm::vec3 origin{}; // mesh space
    float step = 1.0f;  // mesh space units

    /**
     * @param position position in mesh space
     * @return closest grid point.
     */
    [[nodiscard]] GridPoint quantize(glm::vec3 position) const {
        return glm::round((position - origin) / step);
    }
    /// @return mesh space position of the grid point.
    [[nodiscard]] glm::vec3 dequantize(GridPoint point) const {
        return origin + glm::vec3(point) * step;
    }
};

static_assert(sizeof(PositionGrid) == 16);
