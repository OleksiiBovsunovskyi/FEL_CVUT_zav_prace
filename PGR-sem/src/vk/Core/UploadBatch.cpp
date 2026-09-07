module;
#include <vulkan/vulkan.hpp>

#include <string>

module UploadBatch;

import Logger;

namespace {

bool ok(vk::Result r, const char* what) {
    if (r == vk::Result::eSuccess) return true;
    logError(std::string("UploadBatch: ") + what + " failed: VkResult " +
             vk::to_string(r));
    return false;
}

} // namespace

UploadBatch::~UploadBatch() {
    destroy();
}

bool UploadBatch::init(VulkanContext& ctx, BufferManager& buffers) {
    if (commandPool_) {
        logError("UploadBatch: init called twice");
        return false;
    }
    if (!buffers.initialized()) {
        logError("UploadBatch: BufferManager must be initialized first");
        return false;
    }

    ctx_     = &ctx;
    buffers_ = &buffers;
    device_  = ctx.device();

    vk::CommandPoolCreateInfo poolInfo{};
    /* Short-lived, re-recorded per batch. */
    poolInfo.flags            = vk::CommandPoolCreateFlagBits::eTransient |
                                vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = ctx.graphicsQueueFamily();
    if (!ok(device_.createCommandPool(&poolInfo, nullptr, &commandPool_),
            "vkCreateCommandPool")) {
        destroy();
        return false;
    }

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;
    if (!ok(device_.allocateCommandBuffers(&allocInfo, &commandBuffer_),
            "vkAllocateCommandBuffers")) {
        destroy();
        return false;
    }

    /* Unsignalled; begin() resets it and nothing waits before the first submit. */
    vk::FenceCreateInfo fenceInfo{};
    if (!ok(device_.createFence(&fenceInfo, nullptr, &fence_), "vkCreateFence")) {
        destroy();
        return false;
    }

    return true;
}

vk::CommandBuffer UploadBatch::begin() {
    if (!commandBuffer_) {
        logError("UploadBatch: begin called before init");
        return nullptr;
    }
    if (recording_) {
        logError("UploadBatch: begin called while a batch is already open");
        return nullptr;
    }

    if (!ok(static_cast<vk::Result>(vkResetCommandBuffer(static_cast<VkCommandBuffer>(commandBuffer_), 0)),
            "vkResetCommandBuffer"))
        return nullptr;

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    if (!ok(commandBuffer_.begin(&beginInfo), "vkBeginCommandBuffer"))
        return nullptr;

    recording_ = true;
    return commandBuffer_;
}

bool UploadBatch::submitAndWait() {
    if (!recording_) {
        logError("UploadBatch: submitAndWait called without an open batch");
        return false;
    }
    recording_ = false;

    if (!ok(static_cast<vk::Result>(vkEndCommandBuffer(static_cast<VkCommandBuffer>(commandBuffer_))),
            "vkEndCommandBuffer"))
        return false;

    if (!ok(device_.resetFences(1, &fence_), "vkResetFences"))
        return false;

    vk::CommandBufferSubmitInfo cmdInfo{};
    cmdInfo.commandBuffer = commandBuffer_;

    vk::SubmitInfo2 submit{};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos    = &cmdInfo;

    if (!ok(ctx_->graphicsQueue().submit2(1, &submit, fence_),
            "vkQueueSubmit2"))
        return false;

    if (!ok(device_.waitForFences(1, &fence_, vk::True, UINT64_MAX),
            "vkWaitForFences"))
        return false;

    /* Every copy reading from it has completed. */
    buffers_->resetUpload();
    return true;
}

void UploadBatch::destroy() {
    if (fence_) {
        device_.destroyFence(fence_);
        fence_ = nullptr;
    }
    /* Frees the command buffer with it. */
    if (commandPool_) {
        device_.destroyCommandPool(commandPool_);
        commandPool_   = nullptr;
        commandBuffer_ = nullptr;
    }
    recording_ = false;
    device_    = nullptr;
    buffers_   = nullptr;
    ctx_       = nullptr;
}
