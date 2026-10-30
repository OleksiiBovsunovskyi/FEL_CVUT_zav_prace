module;
#include <algorithm>
#include <numbers>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

export module MouseXYComponent;

export import VkScene;

/**
 * Turns the owner Object from cursor deltas.
 */
export class MouseXYComponent : public Component {
public:
    /// Beyond this the forward axis crosses world up and yaw flips.
    static constexpr float PITCH_LIMIT =
        std::numbers::pi_v<float> * 0.5f - 0.001f;

    /// @param radiansPerPixel turn applied per pixel of cursor movement.
    explicit MouseXYComponent(float radiansPerPixel = 0.0011f)
        : radiansPerPixel_(radiansPerPixel) {}

    void setSensitivity(float radiansPerPixel) { radiansPerPixel_ = radiansPerPixel; }
    [[nodiscard]] float getSensitivity() const { return radiansPerPixel_; }

    [[nodiscard]] float getYaw() const { return yaw_; }
    [[nodiscard]] float getPitch() const { return pitch_; }

    void onInput(const InputEventData& event) {
        switch (event.key) {
            case Key::MouseX: pendingX_ += event.value; break;
            case Key::MouseY: pendingY_ += event.value; break;
            default: break;
        }
    }

    /**
     * @param deltaSeconds unused
     */
    void onTick(float /*deltaSeconds*/) {
        if (pendingX_ == 0.0f && pendingY_ == 0.0f) return;

        yaw_   -= pendingX_ * radiansPerPixel_;
        pitch_ = std::clamp(pitch_ - pendingY_ * radiansPerPixel_,
                            -PITCH_LIMIT, PITCH_LIMIT);
        pendingX_ = 0.0f;
        pendingY_ = 0.0f;

        Object& owner = getOwner();
        const glm::vec3 position{owner.getTransform()[3]};

        owner.setTransform(
            glm::translate(glm::mat4{1.0f}, position) *
            glm::rotate(glm::mat4{1.0f}, yaw_,   glm::vec3{0.0f, 1.0f, 0.0f}) *
            glm::rotate(glm::mat4{1.0f}, pitch_, glm::vec3{1.0f, 0.0f, 0.0f}));
    }

private:
    float radiansPerPixel_;

    float yaw_   = 0.0f;
    float pitch_ = 0.0f;

    float pendingX_ = 0.0f;
    float pendingY_ = 0.0f;
};
