module;
#include <glm/glm.hpp>

export module Camera;
import Model;

export class Camera {
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 up;
    float fov;
    float aspect;
    float nearPlane;
    float farPlane;

public:
    explicit Camera(const glm::vec3& pos = glm::vec3(0, 1, 3),
                    const glm::vec3& tgt = glm::vec3(0, 0, 0),
                    const glm::vec3& upDir = glm::vec3(0, 1, 0),
                    float fov = 45.0f, float aspect = 1.0f,
                    float near = 0.1f, float far = 100.0f);

    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getTarget() const;
    [[nodiscard]] glm::vec3 getUp() const;
    [[nodiscard]] float getFov() const;
    [[nodiscard]] float getAspect() const;
    [[nodiscard]] float getNearPlane() const;
    [[nodiscard]] float getFarPlane() const;

    void setPosition(const glm::vec3& pos);
    void setTarget(const glm::vec3& tgt);
    void setUp(const glm::vec3& upDir);
    void setFov(float f);
    void setAspect(float a);
    void setNearPlane(float n);
    void setFarPlane(float f);

    [[nodiscard]] glm::mat4 getView() const;
    [[nodiscard]] glm::mat4 getProjection() const;

    [[nodiscard]] bool isInsideFrustum(const Model* mesh) const;
};
