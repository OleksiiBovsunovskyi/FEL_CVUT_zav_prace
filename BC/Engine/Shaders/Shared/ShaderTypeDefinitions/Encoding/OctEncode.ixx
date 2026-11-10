module;
#include "glm/glm.hpp"

export module encoding;

export namespace glm::octEnc {
    glm::vec2 signNotZero(glm::vec2 v) {
        return { v.x >= 0.0f ? 1.0f : -1.0f,
                 v.y >= 0.0f ? 1.0f : -1.0f };
    }

    // unit vector -> [-1, 1]^2
    glm::vec2 octEncode(glm::vec3 n) {
        n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
        glm::vec2 p(n.x, n.y);
        if (n.z < 0.0f)
            p = (1.0f - glm::abs(glm::vec2(p.y, p.x))) * signNotZero(p);
        return p;
    }

    glm::vec3 octDecode(glm::vec2 p) {
        glm::vec3 n(p.x, p.y, 1.0f - std::abs(p.x) - std::abs(p.y));
        float t = glm::max(-n.z, 0.0f);
        n.x += n.x >= 0.0f ? -t : t;
        n.y += n.y >= 0.0f ? -t : t;
        return glm::normalize(n);
    }
}
