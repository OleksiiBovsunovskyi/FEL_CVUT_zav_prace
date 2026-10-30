module;

#include <array>
#include <cstdint>

export module GpuDrawStats;

import Frame;
import FrameInFlightIndex;
import GPUTypes;
import PerFrameRecord;
import VulkanContext;

/**
 * Owns the GPUDrawCounters the draw shaders add into, one per frame-in-flight slot.
 */
export class GpuDrawStats {
public:
    GpuDrawStats() = default;

    GpuDrawStats(const GpuDrawStats&)            = delete;
    GpuDrawStats& operator=(const GpuDrawStats&) = delete;

    /**
     * Allocates one mapped GPUDrawCounters per frame-in-flight slot.
     * @param ctx supplies the device and the VMA allocator.
     * @return false on a failed allocation.
     */
    [[nodiscard]] bool init(VulkanContext& ctx);

    void destroy();

    /**
     * Publishes the counters this slot recorded and prepares for next recording
     * @param recording active frame recording.
     * @param instanceCount instances this recording submits.
     * @return the address the draw shaders add into, null before GpuDrawStats::init().
     */
    [[nodiscard]] GpuPtr<GPUDrawCounters> beginFrame(Frame::Recording& recording,
                                                     uint32_t instanceCount);

    /**
     * Makes this recording's counters readable by the host once it completes.
     * @param recording active frame recording.
     * @note Records a pipeline barrier
     */
    void endFrame(Frame::Recording& recording);

    /// Draws the object, meshlet and triangle counters into the current ImGui window.
    void drawUI() const;

private:
    PerFrameRecord<GPUDrawCounters>    records_;
    /// Slots holding counters of a submitted recording.
    std::array<bool, FRAMES_IN_FLIGHT> written_{};
    /// Counters of the most recently completed recording.
    GPUDrawCounters                    latest_{};
};
