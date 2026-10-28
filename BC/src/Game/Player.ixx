module;
#include <glm/glm.hpp>

export module Player;

export import VkScene;
export import WASDComponent;
export import MouseXYComponent;

/**
* Player object, owning camera and able to move.
 */
export class Player : public Object {
public:
    /**
     * @param metresPerSecond movement speed.
     * @param radiansPerPixel look sensitivity.
     */
    explicit Player(float metresPerSecond = 1.0f, float radiansPerPixel = 0.0011f)
        : camera_(addComponent(CameraComponent{})),
          movement_(addComponent(WASDComponent{metresPerSecond})),
          look_(addComponent(MouseXYComponent{radiansPerPixel})) {}

    [[nodiscard]] CameraComponent&  getCamera()   { return camera_; }
    [[nodiscard]] WASDComponent&    getMovement() { return movement_; }
    [[nodiscard]] MouseXYComponent& getLook()     { return look_; }

private:
    CameraComponent&  camera_;
    WASDComponent&    movement_;
    MouseXYComponent& look_;
};
