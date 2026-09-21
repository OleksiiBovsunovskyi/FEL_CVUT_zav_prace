module;
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module VkScene:CameraComponent;

export import :Component;

export class Scene;

/**
 *First created camera wins
 */
export class CameraComponent : public Component {
public:
    static constexpr float NEAR_PLANE = 0.01f;
    static constexpr float FAR_PLANE  = 10000.0f;

    static_assert(NEAR_PLANE > 0.0f && FAR_PLANE > NEAR_PLANE,
                  "a perspective projection needs a positive near plane in "
                  "front of the far one;");

    /// @param fovY vertical field of view, radians.
    explicit CameraComponent(float fovY = glm::radians(60.0f)) : fovY_(fovY) {}

    /// Gives up the Scene's camera slot if it is the one holding it.
    ~CameraComponent() override;

    /* The slot goes with the value, so the source cannot release a
     * registration it no longer holds. */
    CameraComponent(CameraComponent&& other) noexcept
        : Component(std::move(other)),
          view_(other.view_),
          fovY_(other.fovY_),
          registeredScene_(std::exchange(other.registeredScene_, nullptr)) {}

    void setFovY(float fovY) { fovY_ = fovY; }
    [[nodiscard]] float getFovY() const { return fovY_; }

    /// World-to-view, recomputed whenever either transform changes.
    [[nodiscard]] const glm::mat4& getView() const { return view_; }

    /// @return the eye, in world space.
    [[nodiscard]] glm::vec3 worldPosition() const {
        return glm::vec3(getWorldTransform()[3]);
    }

    /**
     * @param aspect width / height of whatever is being rendered into;
     * @return world-to-clip.
     */
    [[nodiscard]] glm::mat4 viewProjection(float aspect) const {
        glm::mat4 projection =
            glm::perspective(fovY_, aspect, FAR_PLANE, NEAR_PLANE);

        /* Vulkan clip space has +Y down. */
        projection[1][1] *= -1.0f;

        return projection * view_;
    }

protected:
    /// Takes the Scene's camera slot when it is free; the first camera wins.
    void onAddedToScene() override;

    void onWorldTransformChanged() override {
        view_ = glm::inverse(getWorldTransform());
    }

private:
    glm::mat4 view_{1.0f};
    float     fovY_;

   
    Scene* registeredScene_ = nullptr;
};
