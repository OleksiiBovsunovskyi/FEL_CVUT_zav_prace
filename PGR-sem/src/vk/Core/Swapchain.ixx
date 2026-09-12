module;
#include <VkBootstrap.h>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

export module VkSwapchain;

import VulkanContext;
import VkWindow;
export import Frame;

//TODO: is this class actually needed considering Frame.ixx machinery

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

    vk::Format       format()        const { return static_cast<vk::Format>(vkb_.image_format); }
    vk::Extent2D     extent()        const { return vkb_.extent; }
    uint32_t         imageCount()    const { return static_cast<uint32_t>(frames_.size()); }
    uint32_t         minImageCount() const { return vkb_.requested_min_image_count; }

    struct Acquisition {
        vk::Result result;
        Frame* frame;
    };

    /**
     * Acquires a Frame for rendering.
     * @param imageAvailable semaphore to signal on image acquisition.
     * @return Vulkan acquisition result and selected Frame.
     */
    [[nodiscard]] Acquisition acquire(vk::Semaphore imageAvailable);

    /**
     * Presents the submitted Frame after validating it belongs to this Swapchain.
     * @param frame Frame whose image was submitted.
     * @return Vulkan presentation result.
     */
    [[nodiscard]] vk::Result present(Frame& frame);

private:
    struct ImageIndex { uint32_t value; };

    VulkanContext*   ctx_    = nullptr;
    const AppWindow* window_ = nullptr;

    vkb::Swapchain                       vkb_{};
    std::vector<std::unique_ptr<Frame>>   frames_;
    bool presentationFailed_ = false;

    bool build();
    void destroyFrames();
};
