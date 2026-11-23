module;
#include "glm/glm.hpp"
export module PositionGrid;

/// Point on a PositionGrid, in grid steps from the mesh space origin.
export using GridPoint = glm::ivec3;

/**
 * Integer grid the mesh space positions of one model are quantized to.
 * Grid point 0 is the mesh space origin.
 * Position is point * step.
 * step is a power of two.
 ***/
export struct PositionGrid {
    float step = 1.0f;  // mesh space units

    /**
     * @param position position in mesh space
     * @return closest grid point.
     */
    [[nodiscard]] GridPoint quantize(glm::vec3 position) const {
        return glm::round(position / step);
    }
    /// @return mesh space position of the grid point.
    [[nodiscard]] glm::vec3 dequantize(GridPoint point) const {
        return glm::vec3(point) * step;
    }
};

static_assert(sizeof(PositionGrid) == 4);
