module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

module FrameRunner;

import Logger;

bool FrameRunner::checkResult(vk::Result r, const char* what) {
    if (r == vk::Result::eSuccess) return true;

    if (r == vk::Result::eErrorDeviceLost) {
        if (!deviceLost_) {
            deviceLost_ = true;
            logError(std::string("FrameRunner: device lost during ") + what +
                     " - the GPU faulted; nothing after this point is meaningful");
        }
        return false;
    }

    logError(std::string("FrameRunner: ") + what + " failed: VkResult " + vk::to_string(r));
    return false;
}

bool FrameRunner::checkFatal(vk::Result r, const char* what) {
    if (checkResult(r, what)) return true;
    if (deviceLost_) return false;   /* handled; the app closes */
    throw std::runtime_error(std::string("FrameRunner: ") + what +
                             " failed mid-frame: VkResult " + vk::to_string(r));
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

    const vk::Extent2D extent = swapchain_->extent();
    if (extent.width == 0 || extent.height == 0) {
        logError("FrameRunner: refusing to create a zero-sized depth image");
        return false;
    }

    for (DepthImage& depth : depthImages_) {
        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType     = vk::ImageType::e2D;
        imageInfo.format        = DEPTH_FORMAT;
        imageInfo.extent        = vk::Extent3D{extent.width, extent.height, 1};
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.samples       = vk::SampleCountFlagBits::e1;
        imageInfo.tiling        = vk::ImageTiling::eOptimal;
            /* SAMPLED: a compute pass will build a hierarchical-Z from it. */
        imageInfo.usage         = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                                  vk::ImageUsageFlagBits::eSampled;
        imageInfo.sharingMode   = vk::SharingMode::eExclusive;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

        vma::AllocationCreateInfo allocInfo{};
        allocInfo.usage = vma::MemoryUsage::eAutoPreferDevice;

        if (!checkResult(ctx_->allocator().createImage(&imageInfo, &allocInfo,
                                                        &depth.image, &depth.allocation, nullptr),
                         "vmaCreateImage (depth)"))
            return false;

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image    = depth.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format   = DEPTH_FORMAT;
        viewInfo.subresourceRange =
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};

        if (!checkResult(device_.createImageView(&viewInfo, nullptr, &depth.view),
                         "vkCreateImageView (depth)"))
            return false;
    }
    return true;
}

void FrameRunner::destroyDepthImages() {
    for (DepthImage& depth : depthImages_) {
        if (depth.view) device_.destroyImageView(depth.view);
        if (depth.image)
            ctx_->allocator().destroyImage(depth.image, depth.allocation);
        depth = {};
    }
}

bool FrameRunner::createCommandObjects() {
    vk::CommandPoolCreateInfo poolInfo{};
    /* Re-recorded from scratch every frame. */
    poolInfo.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = ctx_->graphicsQueueFamily();
    if (!checkResult(device_.createCommandPool(&poolInfo, nullptr, &commandPool_), "vkCreateCommandPool"))
        return false;

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool        = commandPool_;
    allocInfo.level              = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = FRAMES_IN_FLIGHT;
    return checkResult(device_.allocateCommandBuffers(&allocInfo, commandBuffers_.data()),
              "vkAllocateCommandBuffers");
}

bool FrameRunner::createSyncObjects() {
    vk::SemaphoreCreateInfo semInfo{};

    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;   /* frame 0 must not block */

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        if (!checkResult(device_.createSemaphore(&semInfo, nullptr, &imageAvailable_[i]), "vkCreateSemaphore"))
            return false;
        if (!checkResult(device_.createFence(&fenceInfo, nullptr, &inFlightFences_[i]), "vkCreateFence"))
            return false;
    }

    vk::SemaphoreTypeCreateInfo timelineType{};
    timelineType.semaphoreType = vk::SemaphoreType::eTimeline;
    timelineType.initialValue  = 0;   /* 0 = nothing submitted yet */

    vk::SemaphoreCreateInfo timelineInfo{};
    timelineInfo.pNext = &timelineType;
    if (!checkResult(device_.createSemaphore(&timelineInfo, nullptr, &timeline_),
                     "vkCreateSemaphore (timeline)"))
        return false;

    return recreateRenderFinishedSemaphores();
}

uint64_t FrameRunner::getCompletedSerial() const {
    if (!timeline_) return 0;

    uint64_t value = 0;
    if (device_.getSemaphoreCounterValue(timeline_, &value) != vk::Result::eSuccess)
        return 0;
    return value;
}

bool FrameRunner::waitForSerial(uint64_t serial, uint64_t timeoutNs) const {
    if (!timeline_) return false;
    if (serial == 0) return true;

    vk::SemaphoreWaitInfo wait{};
    wait.semaphoreCount = 1;
    wait.pSemaphores    = &timeline_;
    wait.pValues        = &serial;

    return device_.waitSemaphores(&wait, timeoutNs) == vk::Result::eSuccess;
}

bool FrameRunner::recreateRenderFinishedSemaphores() {
    for (vk::Semaphore s : renderFinished_)
        device_.destroySemaphore(s);
    renderFinished_.assign(swapchain_->imageCount(), nullptr);

    vk::SemaphoreCreateInfo semInfo{};
    for (auto& s : renderFinished_) {
        if (!checkResult(device_.createSemaphore(&semInfo, nullptr, &s), "vkCreateSemaphore"))
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

    vk::Fence fence = inFlightFences_[currentFrame_];
    if (!checkResult(device_.waitForFences(1, &fence, vk::True, UINT64_MAX), "waitForFences"))
        return;

    uint32_t imageIndex = 0;
    const vk::Result acquire =
        device_.acquireNextImageKHR(swapchain_->handle(), UINT64_MAX,
                                    imageAvailable_[currentFrame_], nullptr, &imageIndex);

    if (acquire == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return;   // this frame is lost; the next one draws at the new size
    }
    if (acquire != vk::Result::eSuccess && acquire != vk::Result::eSuboptimalKHR) {
        checkResult(acquire, "vkAcquireNextImageKHR");
        return;
    }

    /* The fence is now unsignalled; only the submit below signals it again. */
    if (!checkFatal(device_.resetFences(1, &fence), "vkResetFences"))
        return;

    vk::CommandBuffer cmd = commandBuffers_[currentFrame_];
    if (!checkFatal(static_cast<vk::Result>(vkResetCommandBuffer(static_cast<VkCommandBuffer>(cmd), 0)),
                    "vkResetCommandBuffer"))
        return;

    if (!recordAndSubmit(cmd, imageIndex, record))
        return;

    const vk::SwapchainKHR swapchainHandle = swapchain_->handle();

    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &renderFinished_[imageIndex];
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchainHandle;
    present.pImageIndices      = &imageIndex;

    const vk::Result presentRes = ctx_->graphicsQueue().presentKHR(&present);
    if (presentRes == vk::Result::eErrorOutOfDateKHR || presentRes == vk::Result::eSuboptimalKHR ||
        framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchain();
    } else {
        checkResult(presentRes, "vkQueuePresentKHR");
    }

    currentFrame_ = (currentFrame_ + 1) % FRAMES_IN_FLIGHT;
}

bool FrameRunner::recordAndSubmit(vk::CommandBuffer cmd, uint32_t imageIndex,
                                  const RecordFn& record) {
    vk::CommandBufferBeginInfo begin{};
    if (!checkFatal(cmd.begin(&begin), "vkBeginCommandBuffer"))
        return false;

    vk::Image image = swapchain_->image(imageIndex);

    /* UNDEFINED: the whole image is either cleared or overdrawn. */
    transitionImage(cmd, image,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eTopOfPipe, {},
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite);

    /* Depth is cleared every frame. */
    const DepthImage& depth = depthImages_[currentFrame_];
    transitionImage(cmd, depth.image,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eTopOfPipe, {},
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::ImageAspectFlagBits::eDepth);

    const RenderTarget_Old target{
        image,
        swapchain_->view(imageIndex),
        swapchain_->format(),
        swapchain_->extent(),
        depth.view,
        DEPTH_FORMAT,
    };

    if (record) record(cmd, target);

    transitionImage(cmd, image,
                    vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe, {});

    if (!checkFatal(static_cast<vk::Result>(vkEndCommandBuffer(static_cast<VkCommandBuffer>(cmd))),
                    "vkEndCommandBuffer"))
        return false;

    vk::SemaphoreSubmitInfo wait{};
    wait.semaphore = imageAvailable_[currentFrame_];
    wait.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;

    /*** This frame's serial. */
    vk::SemaphoreSubmitInfo signals[2]{};
    signals[0].semaphore = renderFinished_[imageIndex];
    signals[0].stageMask = vk::PipelineStageFlagBits2::eAllCommands;
    signals[1].semaphore = timeline_;
    signals[1].value     = submittedSerial_ + 1;
    signals[1].stageMask = vk::PipelineStageFlagBits2::eAllCommands;

    vk::CommandBufferSubmitInfo cmdInfo{};
    cmdInfo.commandBuffer = cmd;

    vk::SubmitInfo2 submit{};
    submit.waitSemaphoreInfoCount   = 1;
    submit.pWaitSemaphoreInfos      = &wait;
    submit.commandBufferInfoCount   = 1;
    submit.pCommandBufferInfos      = &cmdInfo;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos    = signals;

    if (!checkFatal(
            ctx_->graphicsQueue().submit2(1, &submit, inFlightFences_[currentFrame_]),
            "vkQueueSubmit2"))
        return false;

    /* A rejected submit never signals*/
    ++submittedSerial_;
    return true;
}

void FrameRunner::destroy() {
    if (!device_) return;

    destroyDepthImages();

    for (vk::Semaphore s : renderFinished_)
        device_.destroySemaphore(s);
    renderFinished_.clear();

    if (timeline_) {
        device_.destroySemaphore(timeline_);
        timeline_ = nullptr;
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        device_.destroySemaphore(imageAvailable_[i]);
        device_.destroyFence(inFlightFences_[i]);
    }
    imageAvailable_.fill(nullptr);
    inFlightFences_.fill(nullptr);

    device_.destroyCommandPool(commandPool_);
    commandPool_ = nullptr;
}
