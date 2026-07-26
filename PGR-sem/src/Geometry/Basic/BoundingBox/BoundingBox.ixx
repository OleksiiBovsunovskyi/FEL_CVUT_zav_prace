module;
#include "pgr.h"

export module boundingBox;

export struct BoundingBox {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};