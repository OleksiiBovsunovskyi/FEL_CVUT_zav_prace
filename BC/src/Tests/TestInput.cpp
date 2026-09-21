/* Compile-time checks of input subscription and delivery, and one runtime
 * check that the movement components steer their owner. */

#include <cassert>
#include <cmath>
#include <memory>

#include <glm/glm.hpp>

import VkScene;
import Player;

namespace {

/**
 * Example of an object that receives input only (onInput declared + EventCapable).
 */
struct TakesInput : EventCapable {
    float lastValue = 0.0f;
    constexpr void onInput(const InputEventData& event) { lastValue = event.value; }
};

/**
 * Example of an object that should **NOT** receive input (onInput is hidden).
 */
struct TakesInputHidden : EventCapable { protected: void onInput(const InputEventData&) {} };

/**
 * Example of an object receiving both events, as the movement components do.
 */
struct TakesInputAndTicks : EventCapable {
    constexpr void onInput(const InputEventData&) {}
    constexpr void onTick(float) {}
};

static_assert(DeclaresOnInput<TakesInput>);
static_assert(!DeclaresOnInput<TakesInputHidden>,
              "onInput has to be public: the subscription is made from outside "
              "component silently never receives input");

constexpr bool oneSubscribeCoversBothEvents() {
    AllEventSubscriptions subscriptions;

    TakesInputAndTicks both;
    subscriptions.subscribe(both);

    return subscriptions.count<TickEvent>() == 1 &&
           subscriptions.count<InputEvent>() == 1;
}

static_assert(oneSubscribeCoversBothEvents(),
              "one subscribe must reach every event the type declares");

constexpr bool broadcastReachesTheHandler() {
    AllEventSubscriptions subscriptions;

    TakesInput takesInput;
    subscriptions.subscribe(takesInput);
    subscriptions.broadcast<InputEvent>(InputEventData{Key::W, 1.0f});

    return takesInput.lastValue == 1.0f;
}

static_assert(broadcastReachesTheHandler(),
              "broadcast must reach onInput through the stored entry");

/// Holds W for a second, then turns a quarter circle to the left.
bool inputMovesAndTurnsThePlayer() {
    Scene scene;

    constexpr float SPEED = 2.0f;
    Player& player = scene.addObject(std::make_unique<Player>(SPEED));

    scene.input({Key::W, 1.0f});
    scene.tick(1.0f);

    /* Unrotated, so forward is -Z. */
    const glm::vec3 moved{player.getTransform()[3]};
    if (std::abs(moved.z + SPEED) > 1e-4f) return false;

    scene.input({Key::W, 0.0f});

    MouseXYComponent* look = player.getComponent<MouseXYComponent>();
    if (!look) return false;

    const float quarterTurnPixels = 1.5707963f / look->getSensitivity();
    scene.input({Key::MouseX, -quarterTurnPixels});
    scene.tick(1.0f);

    const glm::vec3 forward{-glm::vec3{player.getTransform()[2]}};
    /* Yawed a quarter turn left of -Z, which is -X. */
    return std::abs(forward.x + 1.0f) < 1e-3f &&
           std::abs(glm::vec3{player.getTransform()[3]}.z + SPEED) < 1e-4f;
}

[[maybe_unused]] const bool steeringChecked = [] {
    const bool passed = inputMovesAndTurnsThePlayer();
    assert(passed && "a held movement key translates the Player along its own "
                     "forward axis, and a cursor delta turns it");
    return passed;
}();

} // namespace
