module;
#include <glm/glm.hpp>
export module lightsource.spotlight;

import LightSource;

export class SpotLight : public LightSource {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float constant    = 1.0f;
    float linear      = 0.09f;
    float quadratic   = 0.032f;
    float innerCutoff = 12.5f;
    float outerCutoff = 17.5f;
    float shadowFov   = 35.0f;

public:
    void setPosition(const glm::vec3& pos)                     { position = pos; }
    void setDirection(const glm::vec3& dir)                    { direction = glm::normalize(dir); }
    void setAttenuation(float c, float l, float q)             { constant = c; linear = l; quadratic = q; }
    void setInnerCutoff(float deg)                             { innerCutoff = deg; }
    void setOuterCutoff(float deg)                             { outerCutoff = deg; }
    void setShadowFov(float deg)                               { shadowFov = deg; }

    [[nodiscard]] glm::vec3 getPosition()    const { return position; }
    [[nodiscard]] glm::vec3 getDirection()   const { return direction; }
    [[nodiscard]] float getConstant()        const { return constant; }
    [[nodiscard]] float getLinear()          const { return linear; }
    [[nodiscard]] float getQuadratic()       const { return quadratic; }
    [[nodiscard]] float getInnerCutoff()     const { return innerCutoff; }
    [[nodiscard]] float getOuterCutoff()     const { return outerCutoff; }
    [[nodiscard]] float getShadowFov()       const { return shadowFov; }
};
