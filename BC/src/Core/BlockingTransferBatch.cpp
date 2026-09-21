module;
#include <vulkan/vulkan_core.h>

#include <string>

module BlockingTransferBatch;

import vulkan;
import Logger;

namespace {

bool ok(vk::Result r, const char* what) {
    if (r == vk::Result::eSuccess) return true;
    logError(std::string("BlockingTransferBatch: ") + what + " failed: VkResult " +
             vk::to_string(r));
    return false;
}

} // namespace

BlockingTransferBatch::~BlockingTransferBatch() {
    destroy();
}

bool BlockingTransferBatch::init(VulkanContext& ctx) {
    if (commandPool_) {
        logError("BlockingTransferBatch: init called twice");
        return false;
    }

    ctx_    = &ctx;
    device_ = ctx.device();

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

vk::CommandBuffer BlockingTransferBatch::begin() {
    if (!commandBuffer_) {
        logError("BlockingTransferBatch: begin called before init");
        return nullptr;
    }
    if (recording_) {
        logError("BlockingTransferBatch: begin called while a batch is already open");
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

bool BlockingTransferBatch::submitAndWait() {
    if (!recording_) {
        logError("BlockingTransferBatch: submitAndWait called without an open batch");
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

    return ok(device_.waitForFences(1, &fence_, vk::True, UINT64_MAX),
              "vkWaitForFences");
}

void BlockingTransferBatch::destroy() {
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
    ctx_       = nullptr;
}
