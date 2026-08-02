module;
#include <vulkan/vulkan.h>

#include <string>

module UploadBatch;

import Logger;

namespace {

bool ok(VkResult r, const char* what) {
    if (r == VK_SUCCESS) return true;
    logError(std::string("UploadBatch: ") + what + " failed: VkResult " +
             std::to_string(r));
    return false;
}

} // namespace

UploadBatch::~UploadBatch() {
    destroy();
}

bool UploadBatch::init(VulkanContext& ctx, VK_buffers& buffers) {
    if (commandPool_) {
        logError("UploadBatch: init called twice");
        return false;
    }
    if (!buffers.initialized()) {
        logError("UploadBatch: VK_buffers must be initialized first");
        return false;
    }

    ctx_     = &ctx;
    buffers_ = &buffers;
    device_  = ctx.device();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    /* Short-lived, re-recorded per batch. */
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx.graphicsQueueFamily();
    if (!ok(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
            "vkCreateCommandPool")) {
        destroy();
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (!ok(vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer_),
            "vkAllocateCommandBuffers")) {
        destroy();
        return false;
    }

    /* Unsignalled; begin() resets it and nothing waits before the first submit. */
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (!ok(vkCreateFence(device_, &fenceInfo, nullptr, &fence_), "vkCreateFence")) {
        destroy();
        return false;
    }

    return true;
}

VkCommandBuffer UploadBatch::begin() {
    if (!commandBuffer_) {
        logError("UploadBatch: begin called before init");
        return VK_NULL_HANDLE;
    }
    if (recording_) {
        logError("UploadBatch: begin called while a batch is already open");
        return VK_NULL_HANDLE;
    }

    if (!ok(vkResetCommandBuffer(commandBuffer_, 0), "vkResetCommandBuffer"))
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!ok(vkBeginCommandBuffer(commandBuffer_, &beginInfo), "vkBeginCommandBuffer"))
        return VK_NULL_HANDLE;

    recording_ = true;
    return commandBuffer_;
}

bool UploadBatch::submitAndWait() {
    if (!recording_) {
        logError("UploadBatch: submitAndWait called without an open batch");
        return false;
    }
    recording_ = false;

    if (!ok(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer"))
        return false;

    if (!ok(vkResetFences(device_, 1, &fence_), "vkResetFences"))
        return false;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = commandBuffer_;

    VkSubmitInfo2 submit{};
    submit.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos    = &cmdInfo;

    if (!ok(vkQueueSubmit2(ctx_->graphicsQueue(), 1, &submit, fence_),
            "vkQueueSubmit2"))
        return false;

    if (!ok(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX),
            "vkWaitForFences"))
        return false;

    /* Every copy reading from it has completed. */
    buffers_->resetUpload();
    return true;
}

void UploadBatch::destroy() {
    if (fence_) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    /* Frees the command buffer with it. */
    if (commandPool_) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_   = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
    }
    recording_ = false;
    device_    = VK_NULL_HANDLE;
    buffers_   = nullptr;
    ctx_       = nullptr;
}
