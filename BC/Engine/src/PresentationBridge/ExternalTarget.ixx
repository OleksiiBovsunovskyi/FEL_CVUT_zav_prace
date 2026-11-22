module;

#include <cstdint>
#include <memory>
#include <vector>

export module ExternalTarget;

import vulkan;
import VulkanContext;
export import PresentTarget;
import PresentationBridge;

/**
 * PresentTarget backed by dedicated Win32 external-memory images and exportable semaphores.
 * Shares GPU handles with an external host through PresentationBridge.
 */
export class ExternalTarget : public PresentTarget {
public:
    ExternalTarget() = default;
    ~ExternalTarget() override { destroy(); }

    ExternalTarget(const ExternalTarget&)            = delete;
    ExternalTarget& operator=(const ExternalTarget&) = delete;

    /// @param extent initial size in texels, at least 1x1.
    bool init(VulkanContext& ctx, PresentationBridge& bridge, vk::Extent2D extent);
    void destroy();

    [[nodiscard]] vk::Format      format()        const override { return VIEW_FORMAT; }
    [[nodiscard]] vk::Extent2D    extent()        const override { return extent_; }
    [[nodiscard]] uint32_t        imageCount()    const override { return static_cast<uint32_t>(slots_.size()); }
    [[nodiscard]] uint32_t        minImageCount() const override { return imageCount(); }
    [[nodiscard]] Frame::ImageUse finalImageUse() const override;
    bool recreate() override;
    [[nodiscard]] Acquisition acquire(vk::Semaphore imageAvailable) override;
    [[nodiscard]] vk::Result  present(Frame& frame) override;

private:
    /// Format used by Avalonia presenter.
    static constexpr vk::Format IMAGE_FORMAT = vk::Format::eB8G8R8A8Unorm;
    static constexpr vk::Format VIEW_FORMAT  = vk::Format::eB8G8R8A8Srgb;
    static constexpr uint32_t   IMAGE_COUNT  = 3;

    struct Slot {
        vk::Image              image{};
        vk::DeviceMemory       memory{};
        vk::DeviceSize         memorySize = 0;
        vk::Semaphore          renderDone{};
        vk::Semaphore          avaloniaDone{};
        void*                  memoryHandle       = nullptr;
        void*                  renderDoneHandle   = nullptr;
        void*                  avaloniaDoneHandle = nullptr;
        std::unique_ptr<Frame> frame;
    };

    VulkanContext*      ctx_    = nullptr;
    PresentationBridge* bridge_ = nullptr;
    vk::Extent2D        extent_{};
    std::vector<Slot>   slots_;

    bool build();
    void destroy_images();
    [[nodiscard]] uint32_t find_device_memory_type(uint32_t type_bits) const;
};
