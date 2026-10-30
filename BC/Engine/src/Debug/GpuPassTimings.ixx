module;

#include <array>
#include <cstdint>
#include <span>

export module GpuPassTimings;

import vulkan;
import Frame;
import FrameInFlightIndex;
import VulkanContext;

/**
 * GPU time between marks recorded into one frame's command buffer.
 * Timestamps are read back when the frame-in-flight slot is reused, so the
 * published intervals are FRAMES_IN_FLIGHT recordings old.
 */
export class GpuPassTimings {
public:
    /** One recorded interval. */
    struct Timing {
        const char* name = nullptr;
        float milliseconds = 0.0f;
    };

    GpuPassTimings() = default;
    ~GpuPassTimings() { destroy(); }

    GpuPassTimings(const GpuPassTimings&)            = delete;
    GpuPassTimings& operator=(const GpuPassTimings&) = delete;

    /**
     * Creates one timestamp query pool per frame-in-flight slot.
     * @param ctx supplies the device, the graphics queue family and the
     *        timestamp period.
     * @return false when the graphics queue family writes no timestamp bits or
     *         a query pool failed; marks then record nothing.
     */
    [[nodiscard]] bool init(VulkanContext& ctx);

    void destroy();

    /**
     * Publishes the intervals this slot recorded when it was last used and
     * opens a new chain of marks.
     * @param recording active frame recording.
     * @note Records vkCmdResetQueryPool, which is illegal inside a dynamic
     *       rendering instance.
     */
    void beginFrame(Frame::Recording& recording);

    /**
     * Closes the interval that began at the previous mark.
     * @param name interval label.
     * @note The label must outlive the two recordings it takes to publish.
     */
    void mark(vk::CommandBuffer commandBuffer, const char* name);

    /// @return the intervals of the most recently completed recording.
    [[nodiscard]] std::span<const Timing> timings() const {
        return {timings_.data(), timingCount_};
    }

private:
    /// Marks one recording can write, the opening one included.
    static constexpr uint32_t MAX_MARKS = 8;

    struct Slot {
        vk::QueryPool pool;
        std::array<const char*, MAX_MARKS> names{};
        uint32_t markCount = 0;
    };

    void publish(Slot& slot);

    vk::Device device_ = nullptr;
    std::array<Slot, FRAMES_IN_FLIGHT> slots_{};
    std::array<Timing, MAX_MARKS> timings_{};
    uint32_t timingCount_ = 0;
    Slot* current_ = nullptr;
    /// vk::PhysicalDeviceLimits::timestampPeriod; nanoseconds per tick.
    float nanosecondsPerTick_ = 0.0f;
};
