module;
#include <glm/glm.hpp>
export module lightsource.directional;

import LightSource;

export class DirectionalLight : public LightSource {
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float shadowOrthoSize = 100.0f;

public:
    void setDirection(const glm::vec3& dir)  { direction = glm::normalize(dir); }
    void setShadowOrthoSize(float s)         { shadowOrthoSize = s; }

    [[nodiscard]] glm::vec3 getDirection()       const { return direction; }
    [[nodiscard]] float     getShadowOrthoSize() const { return shadowOrthoSize; }
};
