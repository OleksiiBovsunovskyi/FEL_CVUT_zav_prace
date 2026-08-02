module;
#include <vulkan/vulkan.h>

export module UploadBatch;

import VulkanContext;
import VK_Buffers;

/**
 * Gets data into GPU memory the CPU cannot write to directly.
 *
 * Mesh::upload copies nothing itself: it writes into the host-visible staging
 * buffer and records a copy command. The data has to stay in staging until the
 * GPU runs that command, which is why staging is released by submitAndWait()
 * alone.
 *
 *     VkCommandBuffer cmd = batch.begin();
 *     mesh.upload(buffers, cmd, data, material);   // records, copies nothing
 *     batch.submitAndWait();                       // runs it, frees staging
 *
 * A failed upload may only have run out of staging space; submitting and
 * retrying on a fresh batch clears that, unless the source is larger than
 * VK_buffers::uploadCapacity() outright.
 *
 * Blocks the CPU until the GPU finishes, which only load time can afford.
 */
export class UploadBatch {
public:
    UploadBatch() = default;
    ~UploadBatch();

    UploadBatch(const UploadBatch&)            = delete;
    UploadBatch& operator=(const UploadBatch&) = delete;

    bool init(VulkanContext& ctx, VK_buffers& buffers);
    void destroy();

    /// VK_NULL_HANDLE on failure or if a batch is already open.
    [[nodiscard]] VkCommandBuffer begin();

    /**
     * Ends recording, submits, blocks until the GPU is done, then releases the
     * staging buffer. Safe on an empty batch.
     *
     * Every BufferSlice from allocateUpload() since the last flush dies here.
     */
    bool submitAndWait();

    [[nodiscard]] bool recording() const { return recording_; }

private:
    VulkanContext* ctx_     = nullptr;
    VK_buffers*    buffers_ = nullptr;
    VkDevice       device_  = VK_NULL_HANDLE;

    VkCommandPool   commandPool_   = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence         fence_         = VK_NULL_HANDLE;

    bool recording_ = false;
};
