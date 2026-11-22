module;

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

export module FrameRunner;

import vulkan;
import VulkanContext;
import PresentTarget;
import VkUtil;
import FrameInFlightIndex;
export import Frame;

/**
 * Coordinates acquisition, Frame recording, submission and presentation.
 * Owns acquisition semaphores and the global submission timeline.
 * Per-frame buffer reuse is paced by completed submissions.
 */
export class FrameRunner {
public:
    using RecordFn = Frame::RecordFn;
    using SubmissionSerial = Frame::SubmissionSerial;

    /**
     * Fired after the present target and its Frames are rebuilt.
     */
    using TargetRecreatedFn = std::function<void(vk::Extent2D, vk::Format)>;

    FrameRunner() = default;
    ~FrameRunner() = default;

    FrameRunner(const FrameRunner&) = delete;
    FrameRunner& operator=(const FrameRunner&) = delete;

    bool init(VulkanContext& ctx, PresentTarget& target);
    void destroy();

    void setTargetRecreatedCallback(TargetRecreatedFn cb) {
        onTargetRecreated_ = std::move(cb);
    }

    void notifyResized() { framebufferResized_ = true; }
    void drawFrame(const RecordFn& record);

    /// @return serial of the latest successful graphics submission.
    [[nodiscard]] SubmissionSerial submittedSerial() const { return submittedSerial_; }
    /// @return highest completed serial, or zero on query failure.
    [[nodiscard]] SubmissionSerial getCompletedSerial() const;
    /// @return whether the requested submission completed within the timeout.
    bool waitForSerial(SubmissionSerial serial, uint64_t timeoutNs = UINT64_MAX) const;
    bool deviceLost() const { return deviceLost_; }

private:
    PresentTarget* target_ = nullptr;
    vk::Device device_ = nullptr;

    std::array<vk::Semaphore, FRAMES_IN_FLIGHT> imageAvailable_{};
    std::array<SubmissionSerial, FRAMES_IN_FLIGHT> frameSubmissions_{};
    vk::Semaphore timeline_ = nullptr;
    SubmissionSerial submittedSerial_ = 0;

    FrameInFlightIndex currentFrame_ = FrameInFlightIndex::first();
    bool framebufferResized_ = false;
    bool deviceLost_ = false;
    TargetRecreatedFn onTargetRecreated_;

    bool checkResult(vk::Result result, const char* operation);
    bool checkFatal(vk::Result result, const char* operation);
    bool createSyncObjects();
    bool checkImageCount() const;
    bool recreateTarget() const;
};
