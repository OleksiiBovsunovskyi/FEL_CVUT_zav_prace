module;

export module BlockingTransferBatch;

import vulkan;
import VulkanContext;

/**
 * Owns the command pool, command buffer and fence one blocking transfer needs.
 * Satisfies the TransferSubmitter concept.
 * Recorded copies run on the graphics queue.
 * One batch is open at a time, between BlockingTransferBatch::begin() and
 * BlockingTransferBatch::submitAndWait().
 */
export class BlockingTransferBatch {
public:
    BlockingTransferBatch() = default;
    ~BlockingTransferBatch();

    BlockingTransferBatch(const BlockingTransferBatch&)            = delete;
    BlockingTransferBatch& operator=(const BlockingTransferBatch&) = delete;

    bool init(VulkanContext& ctx);
    void destroy();

    /**
     * Opens a batch and begins recording into its command buffer.
     * @return the command buffer, nullptr on failure or while a batch is open.
     */
    [[nodiscard]] vk::CommandBuffer begin();

    /**
     * Ends recording, submits the batch, and blocks until the GPU has finished.
     * @return false when no batch is open, or on a failed submission.
     */
    bool submitAndWait();

    /// @return true while a batch is open.
    [[nodiscard]] bool recording() const { return recording_; }

private:
    VulkanContext* ctx_    = nullptr;
    vk::Device     device_ = nullptr;

    vk::CommandPool   commandPool_   = nullptr;
    vk::CommandBuffer commandBuffer_ = nullptr;
    vk::Fence         fence_         = nullptr;

    bool recording_ = false;
};
