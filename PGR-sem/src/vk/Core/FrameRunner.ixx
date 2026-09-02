module;
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

export module FrameRunner;

import VulkanContext;
import VkSwapchain;
import VkUtil;

/**
 * Drives one frame: acquire an image, record into it, submit, present.
 *
 * Owns every piece of frame pacing - command buffers, fences, semaphores
 * A renderer receives an already-begun command buffer and a target that is already in COLOR_ATTACHMENT_OPTIMAL
 */
export class FrameRunner {
public:
    static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

    /**
     * Records this frame's work. The command buffer is begun and ended by the
     * caller; the target is renderable on entry and presented on return.
     */
    using RecordFn = std::function<void(VkCommandBuffer, const RenderTarget&)>;

    /**
     * Fired from inside drawFrame() after a swapchain rebuild. Extent changes
     * need no pipeline rebuild; a format change invalidates every pipeline
     * built against it.
     */
    using SwapchainRecreatedFn = std::function<void(VkExtent2D, VkFormat)>;

    FrameRunner() = default;
    ~FrameRunner() = default;

    FrameRunner(const FrameRunner&)            = delete;
    FrameRunner& operator=(const FrameRunner&) = delete;

    bool init(VulkanContext& ctx, Swapchain& swapchain);
    void destroy();

    void setSwapchainRecreatedCallback(SwapchainRecreatedFn cb) {
        onSwapchainRecreated_ = std::move(cb);
    }

    /**
     * Hook up to the window resize callback. The rebuild happens on the next
     * present, making this safe from a GLFW callback mid-frame.
     */
    void notifyResized() { framebufferResized_ = true; }

    void drawFrame(const RecordFn& record);

    uint32_t frameIndex() const { return currentFrame_; }

    /**
     * Serial of the most recent submission; monotonic.
     *
     * With completedSerial(), the clock for deferred buffer retirement. Once
     * per frame the owner of a BufferManager should call:
     *
     *     buffers.setRetirementSerial(frames.submittedSerial());
     *     buffers.collect(frames.completedSerial());
     */
    [[nodiscard]] uint64_t submittedSerial() const { return submittedSerial_; }

    /**
     * @return highest serial the GPU has finished, or 0 if the counter cannot be
     *         read, which collects nothing.
     */
    [[nodiscard]] uint64_t getCompletedSerial() const;

    /**
     * Blocks until completedSerial() >= serial.
     * @return false on timeout or error.
     */
    bool waitForSerial(uint64_t serial, uint64_t timeoutNs = UINT64_MAX) const;

    /**
     * True once the GPU has faulted.
     */
    bool deviceLost() const { return deviceLost_; }

private:
    /**
     * One per frame in flight. Concurent frames should not share.
     */
    struct DepthImage {
        VkImage       image      = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView   view       = VK_NULL_HANDLE;
    };

    VulkanContext* ctx_       = nullptr;
    Swapchain*     swapchain_ = nullptr;
    VkDevice       device_    = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FRAMES_IN_FLIGHT> commandBuffers_{};
    std::array<VkFence,         FRAMES_IN_FLIGHT> inFlightFences_{};
    std::array<VkSemaphore,     FRAMES_IN_FLIGHT> imageAvailable_{};
    std::array<DepthImage,      FRAMES_IN_FLIGHT> depthImages_{};

    /// One per swapchain image.
    std::vector<VkSemaphore> renderFinished_;

    VkSemaphore timeline_       = VK_NULL_HANDLE;
    uint64_t    submittedSerial_ = 0;

    uint32_t currentFrame_       = 0;
    bool     framebufferResized_ = false;
    bool     deviceLost_         = false;

    SwapchainRecreatedFn onSwapchainRecreated_;

    /**
     * Logs, and latches deviceLost_ on VK_ERROR_DEVICE_LOST
     */
    bool checkResult(VkResult r, const char* what);

    /**
     * For the stretch between resetting the fence and the submit that signals
     * it, where returning would strand the fence and deadlock the slot.
     *
     * @return false on device loss; anything else throws.
     */
    bool checkFatal(VkResult r, const char* what);

    bool createCommandObjects();
    bool createSyncObjects();
    bool recreateRenderFinishedSemaphores();

    bool recreateDepthImages();
    void destroyDepthImages();

    /// Re-checked after every rebuild; a rebuild can return fewer images.
    bool checkImageCount() const;

    bool recreateSwapchain();

    bool recordAndSubmit(VkCommandBuffer cmd, uint32_t imageIndex, const RecordFn& record);
};
