module;
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

module VkSwapchain;

import vulkan;
import Logger;
import renderTarget;

bool Swapchain::init(VulkanContext& ctx, const AppWindow& window) {
    ctx_    = &ctx;
    window_ = &window;
    return build();
}

bool Swapchain::build() {
    int w = 0, h = 0;
    window_->getFramebufferSize(w, h);

    vkb::SwapchainBuilder builder{ctx_->vkbDevice()};
    auto ret = builder
        /* _SRGB makes the hardware apply the transfer function on write. */
        .set_desired_format(VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB,
                                               VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
        .set_desired_extent(static_cast<uint32_t>(w), static_cast<uint32_t>(h))
        .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        .set_old_swapchain(vkb_)
        .build();
    if (!ret) {
        logError("vkb swapchain: " + ret.error().message());
        return false;
    }

    /* Destroyable only now the new one exists. */
    vkb::destroy_swapchain(vkb_);
    vkb_ = ret.value();

    auto imagesRet = vkb_.get_images();
    if (!imagesRet) {
        logError("swapchain images: " + imagesRet.error().message());
        return false;
    }

    const auto& images = imagesRet.value();
    frames_.reserve(images.size());
    for (vk::Image image : images) {
        frames_.push_back(std::make_unique<Frame>(*ctx_, image, format(), extent()));
    }
    presentationFailed_ = false;
    return true;
}

bool Swapchain::recreate() {
    window_->waitWhileMinimized();
    ctx_->waitIdle();

    destroyFrames();
    return build();
}

Swapchain::Acquisition Swapchain::acquire(vk::Semaphore imageAvailable) {
    if (!imageAvailable)
        throw std::invalid_argument("Swapchain::acquire: acquisition semaphore is required");
    if (frames_.empty())
        throw std::logic_error("Swapchain::acquire: no swapchain images exist");
    if (presentationFailed_)
        throw std::logic_error("Swapchain::acquire: presentation failure requires swapchain recreation");
    for (const auto& frame : frames_) {
        if (!frame || frame->failed())
            throw std::logic_error("Swapchain::acquire: swapchain recreation is required");
    }

    ImageIndex index{};
    const vk::Result result = ctx_->device().acquireNextImageKHR(
        vkb_.swapchain, UINT64_MAX, imageAvailable, nullptr, &index.value);
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
        return {result, nullptr};
    if (index.value >= frames_.size())
        throw std::runtime_error("Swapchain::acquire: image index exceeds swapchain images");

    Frame& frame = *frames_[index.value];
    frame.acceptAcquisition(imageAvailable);
    return {result, &frame};
}

vk::Result Swapchain::present(Frame& frame) {
    if (presentationFailed_)
        throw std::logic_error("Swapchain::present: presentation failure requires swapchain recreation");

    const auto member = std::find_if(frames_.begin(), frames_.end(),
                                     [&frame](const auto& candidate) {
                                         return candidate.get() == &frame;
                                     });
    if (member == frames_.end())
        throw std::invalid_argument("Swapchain::present: Frame does not belong to this Swapchain");

    const vk::Semaphore finished = frame.release();
    const vk::SwapchainKHR swapchain = vkb_.swapchain;
    const ImageIndex index{static_cast<uint32_t>(member - frames_.begin())};
    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &index.value;
    const vk::Result result = ctx_->graphicsQueue().presentKHR(&present);
    presentationFailed_ = result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR &&
                          result != vk::Result::eErrorOutOfDateKHR;
    return result;
}

void Swapchain::destroyFrames() {
    frames_.clear();
}

void Swapchain::destroy() {
    destroyFrames();
    vkb::destroy_swapchain(vkb_);
    vkb_ = {};
}
