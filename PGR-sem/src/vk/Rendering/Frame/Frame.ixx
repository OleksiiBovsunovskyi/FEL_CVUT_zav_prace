module;
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <functional>

export module Frame;
export import FrameInFlightIndex;
import renderTarget;
import VulkanContext;

/**
 * Owns recording and synchronization for one borrowed image.
 * The color target and its views are owned by Frame.
 */
export class Frame {
public:
    using SubmissionSerial = uint64_t;

    /**
     * Describes the image layout and memory use at a submission boundary.
     */
    struct ImageUse {
        vk::ImageLayout layout;
        vk::PipelineStageFlags2 stageMask;
        vk::AccessFlags2 accessMask;
    };

    /**
     * Grants access to drawing commands during Frame::recordAndSubmit().
     * @note References and command buffers must remain within the recording callback.
     */
    class Recording {
    public:
        Recording(const Recording&) = delete;
        Recording& operator=(const Recording&) = delete;

        /// @return the active command buffer.
        [[nodiscard]] vk::CommandBuffer commandBuffer() const;
        /// @return the color attachment size in texels.
        [[nodiscard]] vk::Extent2D extent() const;
        /// @return the reusable resource slot valid for this recording.
        [[nodiscard]] FrameInFlightIndex frameInFlight() const {
            frame_.require(State::Recording, "Recording::frameInFlight");
            return frameInFlight_;
        }

        /**
         * @param resources reusable resources indexed by the active frame-in-flight slot.
         * @return the resource selected for this recording.
         */
        template <class T>
        [[nodiscard]] T& select(
            std::array<T, FRAMES_IN_FLIGHT>& resources) const {
            frame_.require(State::Recording, "Recording::select");
            return frameInFlight_.select(resources);
        }

        /**
         * Transitions the color target and describes it as a cleared attachment.
         * @param clearColor color to clear the target to.
         * @return color attachment descriptor.
         */
        [[nodiscard]] vk::RenderingAttachmentInfo colorAttachment(vk::ClearColorValue clearColor);

    private:
        friend class Frame;
        Recording(Frame& frame, FrameInFlightIndex frameInFlight)
            : frame_(frame), frameInFlight_(frameInFlight) {}
        Frame& frame_;
        FrameInFlightIndex frameInFlight_;
    };

    using RecordFn = std::function<void(Recording&)>;

    /**
     * Names the global timeline signal and final image use attached to this submission.
     */
    struct Submission {
        vk::Semaphore timeline;
        SubmissionSerial serial;
        ImageUse finalImageUse;
    };

    /**
     * Constructs the target, command buffer, completion fence and image-use semaphore.
     * @param ctx context supplying the device and graphics queue.
     * @param image image to borrow.
     * @param format texel format.
     * @param extent size in texels.
     * @note The device and borrowed image must outlive Frame.
     */
    Frame(VulkanContext& ctx, vk::Image image, vk::Format format, vk::Extent2D extent);
    ~Frame();

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    /**
     * Accepts the semaphore granting this image command access.
     * @param imageAvailable semaphore signaled before this image can be used.
     * @note The semaphore must outlive the subsequent submission.
     */
    void acceptAcquisition(vk::Semaphore imageAvailable);

    /// @return whether this Frame is unusable after a recording or submission failure.
    [[nodiscard]] bool failed() const;

    /**
     * Waits for earlier rendering, records commands and submits them with completion signals.
     * @param record callback recording this image's drawing commands.
     * @param frameInFlight reusable resource slot valid for this recording.
     * @param submission global timeline signal and final image use for the submission.
     * @note A recording or submission failure disables this Frame until replacement.
     */
    [[nodiscard]] vk::Result recordAndSubmit(
        const RecordFn& record, FrameInFlightIndex frameInFlight,
        Submission submission);

    /**
     * Relinquishes submitted image access and returns its completion semaphore.
     */
    [[nodiscard]] vk::Semaphore release();

    /// @return graphics completion status; success, not-ready, or a Vulkan error.
    [[nodiscard]] vk::Result completionStatus() const;
    /// @return the result of waiting for this image's submitted graphics work.
    [[nodiscard]] vk::Result waitUntilComplete() const;

private:
    enum class State { Ready, Acquired, Recording, Submitted, Released, Failed };
    void require(State expected, const char* operation) const;
    void transition(ImageUse imageUse);
    [[nodiscard]] vk::Result beginRecording();
    [[nodiscard]] vk::Result finishRecording(ImageUse imageUse);
    [[nodiscard]] vk::Result submit(Submission submission);

    vk::Device device_;
    vk::Queue queue_;
    ColorRenderTarget frameColor_;
    vk::UniqueCommandPool commandPool_;
    vk::CommandBuffer commandBuffer_;
    vk::UniqueFence completion_;
    vk::UniqueSemaphore renderFinished_;
    vk::Semaphore imageAvailable_;

    State state_ = State::Ready;
    bool submitted_ = false;
    ImageUse imageUse_{vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits2::eNone, {}};
    ImageUse recordingUse_{vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits2::eNone, {}};
};
