module;
#include <glm/glm.hpp>
export module lightsource.point;

import LightSource;

export class PointLight : public LightSource {
    glm::vec3 position{0.0f};
    float constant  = 1.0f;
    float linear    = 0.09f;
    float quadratic = 0.032f;
    float shadowFov = 90.0f;

public:
    void setPosition(const glm::vec3& pos)                     { position = pos; }
    void setAttenuation(float c, float l, float q)             { constant = c; linear = l; quadratic = q; }
    void setShadowFov(float deg)                               { shadowFov = deg; }

    [[nodiscard]] glm::vec3 getPosition()  const { return position; }
    [[nodiscard]] float getConstant()      const { return constant; }
    [[nodiscard]] float getLinear()        const { return linear; }
    [[nodiscard]] float getQuadratic()     const { return quadratic; }
    [[nodiscard]] float getShadowFov()     const { return shadowFov; }
};
