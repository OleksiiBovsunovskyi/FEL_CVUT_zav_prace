module;
#include <VkBootstrap.h>

#include <vector>

export module VkSwapchain;

import VulkanContext;
import VkWindow;

/**
 * The images the compositor presents, plus their views and the rebuild path.
 */
export class Swapchain {
public:
    Swapchain() = default;
    ~Swapchain() = default;

    Swapchain(const Swapchain&)            = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    bool init(VulkanContext& ctx, const AppWindow& window);

    /**
     * Blocks while the window is minimised, waits for the device to go idle,
     * then rebuilds at the current framebuffer size. Every per-image resource
     * held elsewhere (semaphores, attachments) must be rebuilt after this.
     */
    bool recreate();

    void destroy();

    VkSwapchainKHR handle()        const { return vkb_.swapchain; }
    VkFormat       format()        const { return vkb_.image_format; }
    VkExtent2D     extent()        const { return vkb_.extent; }
    uint32_t       imageCount()    const { return static_cast<uint32_t>(images_.size()); }
    uint32_t       minImageCount() const { return vkb_.requested_min_image_count; }

    VkImage     image(uint32_t i) const { return images_[i]; }
    VkImageView view(uint32_t i)  const { return views_[i]; }

    const std::vector<VkImage>&     images() const { return images_; }
    const std::vector<VkImageView>& views()  const { return views_; }

private:
    VulkanContext*   ctx_    = nullptr;
    const AppWindow* window_ = nullptr;

    vkb::Swapchain           vkb_{};
    std::vector<VkImage>     images_;
    std::vector<VkImageView> views_;

    bool build();
    void destroyViews();
};
