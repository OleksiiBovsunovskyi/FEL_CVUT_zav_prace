module;
#include <glm/glm.hpp>
export module LightSource;

export class LightSource {
private:
    glm::vec3 color{1.0f};

    // Shadow map
    bool      castsShadow  = false;
    glm::vec3 shadowTarget = glm::vec3(0.0f);
    float     shadowNear   = 1.0f;
    float     shadowFar    = 2000.0f;

public:
    virtual ~LightSource() = default;

    void setColor(const glm::vec3& col)       { color = col; }
    void setCastsShadow(bool v)               { castsShadow = v; }
    void setShadowTarget(const glm::vec3& t)  { shadowTarget = t; }
    void setShadowNear(float n)               { shadowNear = n; }
    void setShadowFar(float f)                { shadowFar = f; }

    [[nodiscard]] glm::vec3 getColor()        const { return color; }
    [[nodiscard]] bool      getCastsShadow()  const { return castsShadow; }
    [[nodiscard]] glm::vec3 getShadowTarget() const { return shadowTarget; }
    [[nodiscard]] float     getShadowNear()   const { return shadowNear; }
    [[nodiscard]] float     getShadowFar()    const { return shadowFar; }
};
