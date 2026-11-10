module;
#include <cstdint>
export module Triangle;

/**
 * Triangle representation in meshlet space.
 * v0, v1, v2 are offsets of its vertices into the meshlet's vertices.
 ***/
export struct Triangle {
    uint8_t v0{};
    uint8_t v1{};
    uint8_t v2{};
    uint8_t reserved{};
};

static_assert(sizeof(Triangle) == 4);
