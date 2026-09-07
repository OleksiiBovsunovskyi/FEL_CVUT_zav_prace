module;
#include <VkBootstrap.h>
#include <vulkan/vulkan.hpp>

#include <vector>

module VkSwapchain;

import Logger;

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
    auto viewsRet = vkb_.get_image_views();
    if (!viewsRet) {
        logError("swapchain image views: " + viewsRet.error().message());
        return false;
    }

    images_ = imagesRet.value();
    views_  = viewsRet.value();
    return true;
}

bool Swapchain::recreate() {
    window_->waitWhileMinimized();
    ctx_->waitIdle();

    destroyViews();
    return build();
}

void Swapchain::destroyViews() {
    vkb_.destroy_image_views(views_);
    views_.clear();
    images_.clear();   /* owned by the swapchain */
}

void Swapchain::destroy() {
    destroyViews();
    vkb::destroy_swapchain(vkb_);
    vkb_ = {};
}
