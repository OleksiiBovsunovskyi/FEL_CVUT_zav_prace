module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module WASDComponent;

export import VkScene;

/**
 * Moves the owner Object along its own axes while movement keys are held.
 */
export class WASDComponent : public Component {
public:
    /// @param metresPerSecond distance covered per second along one axis.
    explicit WASDComponent(float metresPerSecond = 1.0f)
        : metresPerSecond_(metresPerSecond) {}

    void setSpeed(float metresPerSecond) { metresPerSecond_ = metresPerSecond; }
    [[nodiscard]] float getSpeed() const { return metresPerSecond_; }

    void onInput(const InputEventData& event) {
        const bool down = event.value != 0.0f;
        switch (event.key) {
            case Key::W: forward_ = down; break;
            case Key::S: back_    = down; break;
            case Key::A: left_    = down; break;
            case Key::D: right_   = down; break;
            case Key::E:     up_      = down; break;
            case Key::Q:     down_    = down; break;
            default: break;
        }
    }

    void onTick(float deltaSeconds) {
        const glm::vec3 axes = glm::vec3{
            static_cast<float>(right_)   - static_cast<float>(left_),
            static_cast<float>(up_)      - static_cast<float>(down_),
            static_cast<float>(forward_) - static_cast<float>(back_)};

        if (axes == glm::vec3{0.0f} || deltaSeconds <= 0.0f) return;

        Object&          owner     = getOwner();
        Transform        moved     = owner.getTransform();

        /// The owner's forward is its -Z axis.
        const glm::vec3 direction =
            moved.rotation * glm::vec3{1.0f, 0.0f, 0.0f} * axes.x +
            WORLD_UP                                      * axes.y -
            moved.rotation * glm::vec3{0.0f, 0.0f, 1.0f} * axes.z;

        moved.position += glm::normalize(direction) * metresPerSecond_ * deltaSeconds;
        owner.setTransform(moved);
    }

private:
    static constexpr glm::vec3 WORLD_UP{0.0f, 1.0f, 0.0f};

    float metresPerSecond_;

    bool forward_ = false;
    bool back_    = false;
    bool left_    = false;
    bool right_   = false;
    bool up_      = false;
    bool down_    = false;
};
