module;
#include <optional>
#include <vector>

export module InputHandler;

import VkWindow;
import VkScene;

/**
 * Turns the window's key and cursor reports into queued InputEventData, adding them to the queue
 */
export class InputHandler {
public:
    /// Installs the key and cursor callbacks on the window.
    void setupWindowInputCallbacks(AppWindow& window) {
        window.setKeyCallback([this](unsigned char ascii, bool down) {
            if (const std::optional<Key> key = mapAscii(ascii))
                pending_.push_back({*key, down ? 1.0f : 0.0f});
        });

        window.setMouseMoveCallback([this](float dx, float dy) {
            if (dx != 0.0f) pending_.push_back({Key::MouseX, dx});
            if (dy != 0.0f) pending_.push_back({Key::MouseY, dy});
        });
    }

    /// Everything that arrived since the last clearPending(), in arrival order.
    [[nodiscard]] const std::vector<InputEventData>& pending() const { return pending_; }

    void clearPending() { pending_.clear(); }

private:
    /// @param ascii the window's key code; unmapped ones produce no event.
    [[nodiscard]] static std::optional<Key> mapAscii(unsigned char ascii) {
        switch (ascii) {
            case 'W':  return Key::W;
            case 'A':  return Key::A;
            case 'S':  return Key::S;
            case 'D':  return Key::D;
            case 'Q':  return Key::Q;
            case 'E':  return Key::E;
            default:   return std::nullopt;
        }
    }

    std::vector<InputEventData> pending_;
};
