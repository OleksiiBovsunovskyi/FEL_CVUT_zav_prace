module;
#include <vulkan/vulkan.hpp>

export module UploadBatch;

import VulkanContext;
import BufferManager;

/**
 * Gets data into GPU memory the CPU cannot write to directly.
 *
 * Mesh::upload copies nothing itself: it writes into the host-visible upload
 * buffer and records a copy command. The data has to stay there until the GPU
 * runs that command, which is why that space is released by submitAndWait()
 * alone.
 *
 *     vk::CommandBuffer cmd = batch.begin();
 *     mesh.upload(buffers, cmd, data, material);   // records, copies nothing
 *     batch.submitAndWait();                       // runs it, frees the space
 *
 * A failed upload may only have run out of upload space; submitting and
 * retrying on a fresh batch clears that, unless the source is larger than
 * BufferManager::uploadCapacity() outright.
 *
 * Blocks the CPU until the GPU finishes, which only load time can afford.
 */
export class UploadBatch {
public:
    UploadBatch() = default;
    ~UploadBatch();

    UploadBatch(const UploadBatch&)            = delete;
    UploadBatch& operator=(const UploadBatch&) = delete;

    bool init(VulkanContext& ctx, BufferManager& buffers);
    void destroy();

    /// nullptr on failure or if a batch is already open.
    [[nodiscard]] vk::CommandBuffer begin();

    /**
     * Ends recording, submits, blocks until the GPU is done, then releases the
     * upload buffer. Safe on an empty batch.
     *
     * Every span from allocateUpload() since the last flush dies here.
     */
    bool submitAndWait();

    [[nodiscard]] bool recording() const { return recording_; }

private:
    VulkanContext*   ctx_     = nullptr;
    BufferManager*   buffers_ = nullptr;
    vk::Device       device_  = nullptr;

    vk::CommandPool   commandPool_   = nullptr;
    vk::CommandBuffer commandBuffer_ = nullptr;
    vk::Fence         fence_         = nullptr;

    bool recording_ = false;
};
