module;

#include <array>
#include <cstdint>

export module FrameInFlightIndex;

/**
 * Identifies one reusable resource slot among the frames the CPU may record ahead.
 */
export constexpr uint32_t FRAMES_IN_FLIGHT = 2;

export class FrameInFlightIndex {
public:
    [[nodiscard]] static constexpr FrameInFlightIndex first() {
        return FrameInFlightIndex{0};
    }

    [[nodiscard]] constexpr FrameInFlightIndex next() const {
        return FrameInFlightIndex{(value_ + 1) % FRAMES_IN_FLIGHT};
    }
    
    /**
     *Selects which resource should be used for this frame
     *e.g. framebuffer used with this frame 
     */
    template <class T>
    [[nodiscard]] constexpr T& select(
        std::array<T, FRAMES_IN_FLIGHT>& resources) const {
        return resources[value_];
    }

private:
    explicit constexpr FrameInFlightIndex(uint32_t value) : value_(value) {}

    uint32_t value_;
};
