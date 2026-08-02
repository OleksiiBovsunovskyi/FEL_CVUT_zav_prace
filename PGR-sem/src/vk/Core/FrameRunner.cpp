module;
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

module FrameRunner;

import Logger;

bool FrameRunner::checkResult(VkResult r, const char* what) {
    if (r == VK_SUCCESS) return true;

    if (r == VK_ERROR_DEVICE_LOST) {
        if (!deviceLost_) {
            deviceLost_ = true;
            logError(std::string("FrameRunner: device lost during ") + what +
                     " - the GPU faulted; nothing after this point is meaningful");
        }
        return false;
    }

    logError(std::string("FrameRunner: ") + what + " failed: VkResult " + std::to_string(r));
    return false;
}

bool FrameRunner::checkFatal(VkResult r, const char* what) {
    if (checkResult(r, what)) return true;
    if (deviceLost_) return false;   /* handled; the app closes */
    throw std::runtime_error(std::string("FrameRunner: ") + what +
                             " failed mid-frame: VkResult " + std::to_string(r));
}

bool FrameRunner::checkImageCount() const {
    if (swapchain_->imageCount() < FRAMES_IN_FLIGHT) {
        logError("FrameRunner: FRAMES_IN_FLIGHT (" + std::to_string(FRAMES_IN_FLIGHT) +
                 ") exceeds the swapchain image count (" +
                 std::to_string(swapchain_->imageCount()) +
                 ");");
        return false;
    }
    return true;
}

bool FrameRunner::init(VulkanContext& ctx, Swapchain& swapchain) {
    ctx_       = &ctx;
    swapchain_ = &swapchain;
    device_    = ctx.device();

    return checkImageCount() && createCommandObjects() && createSyncObjects() &&
           recreateDepthImages();
}

bool FrameRunner::recreateDepthImages() {
    destroyDepthImages();

    const VkExtent2D extent = swapchain_->extent();
    if (extent.width == 0 || extent.height == 0) {
        logError("FrameRunner: refusing to create a zero-sized depth image");
        return false;
    }

    for (DepthImage& depth : depthImages_) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.format        = DEPTH_FORMAT;
        imageInfo.extent        = VkExtent3D{extent.width, extent.height, 1};
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            /* SAMPLED: a compute pass will build a hierarchical-Z from it. */
        imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (!checkResult(vmaCreateImage(ctx_->allocator(), &imageInfo, &allocInfo,
                                        &depth.image, &depth.allocation, nullptr),
                         "vmaCreateImage (depth)"))
            return false;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = depth.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = DEPTH_FORMAT;
        viewInfo.subresourceRange =
            VkImageSubresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

        if (!checkResult(vkCreateImageView(device_, &viewInfo, nullptr, &depth.view),
                         "vkCreateImageView (depth)"))
            return false;
    }
    return true;
}

void FrameRunner::destroyDepthImages() {
    for (DepthImage& depth : depthImages_) {
        if (depth.view) vkDestroyImageView(device_, depth.view, nullptr);
        if (depth.image)
            vmaDestroyImage(ctx_->allocator(), depth.image, depth.allocation);
        depth = {};
    }
}

bool FrameRunner::createCommandObjects() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    /* Re-recorded from scratch every frame. */
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx_->graphicsQueueFamily();
    if (!checkResult(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool"))
        return false;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = FRAMES_IN_FLIGHT;
    return checkResult(vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()),
              "vkAllocateCommandBuffers");
}

bool FrameRunner::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;   /* frame 0 must not block */

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (!checkResult(vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailable_[i]), "vkCreateSemaphore"))
            return false;
        if (!checkResult(vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]), "vkCreateFence"))
            return false;
    }

    VkSemaphoreTypeCreateInfo timelineType{};
    timelineType.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineType.initialValue  = 0;   /* 0 = nothing submitted yet */

    VkSemaphoreCreateInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    timelineInfo.pNext = &timelineType;
    if (!checkResult(vkCreateSemaphore(device_, &timelineInfo, nullptr, &timeline_),
                     "vkCreateSemaphore (timeline)"))
        return false;

    return recreateRenderFinishedSemaphores();
}

uint64_t FrameRunner::getCompletedSerial() const {
    if (!timeline_) return 0;

    uint64_t value = 0;
    if (vkGetSemaphoreCounterValue(device_, timeline_, &value) != VK_SUCCESS)
        return 0;
    return value;
}

bool FrameRunner::waitForSerial(uint64_t serial, uint64_t timeoutNs) const {
    if (!timeline_) return false;
    if (serial == 0) return true;

    VkSemaphoreWaitInfo wait{};
    wait.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait.semaphoreCount = 1;
    wait.pSemaphores    = &timeline_;
    wait.pValues        = &serial;

    return vkWaitSemaphores(device_, &wait, timeoutNs) == VK_SUCCESS;
}

bool FrameRunner::recreateRenderFinishedSemaphores() {
    for (VkSemaphore s : renderFinished_)
        vkDestroySemaphore(device_, s, nullptr);
    renderFinished_.assign(swapchain_->imageCount(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& s : renderFinished_) {
        if (!checkResult(vkCreateSemaphore(device_, &semInfo, nullptr, &s), "vkCreateSemaphore"))
            return false;
    }
    return true;
}

bool FrameRunner::recreateSwapchain() {
    if (!swapchain_->recreate())
        return false;

    /* A rebuild can return a different image count. */
    if (!checkImageCount())
        return false;

    /* Sized per swapchain image. */
    if (!recreateRenderFinishedSemaphores())
        return false;

    /* Sized to the extent. */
    if (!recreateDepthImages())
        return false;

    if (onSwapchainRecreated_)
        onSwapchainRecreated_(swapchain_->extent(), swapchain_->format());
    return true;
}

void FrameRunner::drawFrame(const RecordFn& record) {
    if (deviceLost_) return;

    VkFence fence = inFlightFences_[currentFrame_];
    if (!checkResult(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences"))
        return;

    uint32_t imageIndex = 0;
    const VkResult acquire =
        vkAcquireNextImageKHR(device_, swapchain_->handle(), UINT64_MAX,
                              imageAvailable_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;   // this frame is lost; the next one draws at the new size
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        checkResult(acquire, "vkAcquireNextImageKHR");
        return;
    }

    /* The fence is now unsignalled; only the submit below signals it again. */
    if (!checkFatal(vkResetFences(device_, 1, &fence), "vkResetFences"))
        return;

    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    if (!checkFatal(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer"))
        return;

    if (!recordAndSubmit(cmd, imageIndex, record))
        return;

    const VkSwapchainKHR swapchainHandle = swapchain_->handle();

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &renderFinished_[imageIndex];
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchainHandle;
    present.pImageIndices      = &imageIndex;

    const VkResult presentRes = vkQueuePresentKHR(ctx_->graphicsQueue(), &present);
    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR ||
        framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchain();
    } else {
        checkResult(presentRes, "vkQueuePresentKHR");
    }

    currentFrame_ = (currentFrame_ + 1) % FRAMES_IN_FLIGHT;
}

bool FrameRunner::recordAndSubmit(VkCommandBuffer cmd, uint32_t imageIndex,
                                  const RecordFn& record) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (!checkFatal(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer"))
        return false;

    VkImage image = swapchain_->image(imageIndex);

    /* UNDEFINED: the whole image is either cleared or overdrawn. */
    transitionImage(cmd, image,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    /* Depth is cleared every frame. */
    const DepthImage& depth = depthImages_[currentFrame_];
    transitionImage(cmd, depth.image,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

    const RenderTarget target{
        image,
        swapchain_->view(imageIndex),
        swapchain_->format(),
        swapchain_->extent(),
        depth.view,
        DEPTH_FORMAT,
    };

    if (record) record(cmd, target);

    transitionImage(cmd, image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

    if (!checkFatal(vkEndCommandBuffer(cmd), "vkEndCommandBuffer"))
        return false;

    VkSemaphoreSubmitInfo wait{};
    wait.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = imageAvailable_[currentFrame_];
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    /*** This frame's serial. */
    const VkSemaphoreSubmitInfo signals[2]{
        {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = renderFinished_[imageIndex],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        },
        {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = timeline_,
            .value     = submittedSerial_ + 1,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        },
    };

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{};
    submit.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount   = 1;
    submit.pWaitSemaphoreInfos      = &wait;
    submit.commandBufferInfoCount   = 1;
    submit.pCommandBufferInfos      = &cmdInfo;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos    = signals;

    if (!checkFatal(
            vkQueueSubmit2(ctx_->graphicsQueue(), 1, &submit, inFlightFences_[currentFrame_]),
            "vkQueueSubmit2"))
        return false;

    /* A rejected submit never signals*/
    ++submittedSerial_;
    return true;
}

void FrameRunner::destroy() {
    if (!device_) return;

    destroyDepthImages();

    for (VkSemaphore s : renderFinished_)
        vkDestroySemaphore(device_, s, nullptr);
    renderFinished_.clear();

    if (timeline_) {
        vkDestroySemaphore(device_, timeline_, nullptr);
        timeline_ = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
        vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
    imageAvailable_.fill(VK_NULL_HANDLE);
    inFlightFences_.fill(VK_NULL_HANDLE);

    vkDestroyCommandPool(device_, commandPool_, nullptr);
    commandPool_ = VK_NULL_HANDLE;
}
