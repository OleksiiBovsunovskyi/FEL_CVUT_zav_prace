module;
#include <glm/glm.hpp>

export module QuadBeizer;

/**
 * Quadratic beizer curve
 * Calculates location along the curve at given time in range <0, duration> 
 */
export class QuadBeizerCurve {
    glm::vec3 p0, p1, p2;
    float duration;

public:
    QuadBeizerCurve(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, float duration)
        : p0(p0), p1(p1), p2(p2), duration(duration) {}

    /**
     * Calculates current location along the curve in time <time>
     * @param time - current time in range <0, duration>
     * @returns location along the curve at given time <time>
     */
    [[nodiscard]] glm::vec3 getPathPoint(float time) const {
        const float u = time / duration;
        const float v = 1.0f - u;
        return v * v * p0 + 2.0f * v * u * p1 + u * u * p2;
    }

    [[nodiscard]] float getDuration() const { return duration; }
};
