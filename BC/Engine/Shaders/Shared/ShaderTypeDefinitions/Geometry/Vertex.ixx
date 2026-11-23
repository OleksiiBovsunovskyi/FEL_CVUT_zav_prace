module;
#include "glm/glm.hpp"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
export module Vertex;

import encoding;

/**
 * @param normal unit normal
 * @return unit axes spanning the plane perpendicular to normal, as columns 0 and 1.
 */
glm::mat2x3 tangentPlaneAxes(glm::vec3 normal) {
    float side = normal.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (side + normal.z);
    float b = normal.x * normal.y * a;
    return { glm::vec3(1.0f + side * normal.x * normal.x * a, side * b, -side * normal.x),
             glm::vec3(b, side + normal.y * normal.y * a, -normal.y) };
}

/**
 * @param value value in -1..1
 * @return 11-bit snorm, value = (q - 1023) / 1023.
 */
uint32_t encodeSnorm11(float value) {
    return static_cast<uint32_t>(glm::round(glm::clamp(value, -1.0f, 1.0f) * 1023.0f) + 1023.0f);
}
float decodeSnorm11(uint32_t q) {
    return (static_cast<float>(q) - 1023.0f) / 1023.0f;
}

/**
 * Vertex attributes in meshlet space;
 * the position is in the owning meshlet's position stream.
 * Normal is octahedral, 11-bit snorm per component, value = (q - 1023) / 1023.
 * Tangent is an angle around the normal.
 ***/
export struct Vertex {
    uint32_t normalX : 11;      // octahedral snorm
    uint32_t normalY : 11;      // octahedral snorm
    uint32_t tangentAngle : 8;  // turns around the normal, in 1/256 steps;
    //to be widened once texCoord is quantized or something else frees up bit or 2
    uint32_t tangentSign : 1;   // 1 for bitangent handedness -1
    uint32_t reserved : 1;      // probably for border verts
    uint32_t packed_texCoord{}; // half 16-16

    /**
     * @param normal unit normal
     * @param tangent tangent, its component along normal is discarded
     * @param sign bitangent handedness (+1 / -1)
     */
    void packNormalTangent(glm::vec3 normal, glm::vec3 tangent, float sign = 1.0f) {
        glm::vec2 oct = glm::octEnc::octEncode(normal);
        normalX = encodeSnorm11(oct.x);
        normalY = encodeSnorm11(oct.y);
        glm::mat2x3 axes = tangentPlaneAxes(unpackNormal());
        float turns = glm::atan(glm::dot(tangent, axes[1]), glm::dot(tangent, axes[0])) / glm::two_pi<float>();
        tangentAngle = static_cast<uint32_t>(glm::round(glm::fract(turns) * 256.0f)) & 0xFFu;
        tangentSign = sign >= 0.0f ? 0u : 1u;
    }
    [[nodiscard]] glm::vec3 unpackNormal() const {
        return glm::octEnc::octDecode(glm::vec2(decodeSnorm11(normalX), decodeSnorm11(normalY)));
    }
    /// @return unit tangent in xyz, bitangent handedness (+1 / -1) in w.
    [[nodiscard]] glm::vec4 unpackTangent() const {
        glm::mat2x3 axes = tangentPlaneAxes(unpackNormal());
        float angle = static_cast<float>(tangentAngle) / 256.0f * glm::two_pi<float>();
        return { axes[0] * glm::cos(angle) + axes[1] * glm::sin(angle), tangentSign != 0u ? -1.0f : 1.0f };
    }

    void packTexCoord(glm::vec2 uv) {
        packed_texCoord = glm::packHalf2x16(uv);
    }
    [[nodiscard]] glm::vec2 unpackTexCoord() const {
        return glm::unpackHalf2x16(packed_texCoord);
    }
};

static_assert(sizeof(Vertex) == 8);
