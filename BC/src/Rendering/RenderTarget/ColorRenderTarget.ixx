module;

#include <cstdint>

export module renderTarget:ColorRenderTarget;

import vulkan;
import vk_mem_alloc;
export import :RenderTarget2D;
import Image;

/**
 * A color attachment of one pass.
 */
export class ColorRenderTarget : public RenderTarget2D {
public:
    /**
     * @param allocator VMA allocator the image is allocated from.
     * @param format texel format.
     * @param extent size in texels; both members must be at least 1.
     * @param usage every way the image will ever be used.
     * @param mipLevels levels in the chain.
     * @param samples samples per texel.
     */
    ColorRenderTarget(vma::Allocator allocator, vk::Format format, vk::Extent2D extent,
                      vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment |
                                                  vk::ImageUsageFlagBits::eSampled,
                      uint32_t mipLevels = 1,
                      vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
        : RenderTarget2D(allocator, format, extent, usage, mipLevels, samples) {}

    /**
     * @param device device the views are created on.
     * @param image image to borrow.
     * @param format texel format.
     * @param extent size in texels; both members must be at least 1.
     * @param mipLevels levels in the chain.
     * @param samples samples per texel.
     * @note Borrowed image will not be owned by this class.
     */
    ColorRenderTarget(vk::Device device, vk::Image image, vk::Format format, vk::Extent2D extent,
                      uint32_t mipLevels = 1,
                      vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
        : RenderTarget2D(device, image, format, extent, mipLevels, samples) {}

    /// @return a view of the color aspect. Empty when creation failed.
    [[nodiscard]] ImageRef view() {
        return RenderTarget::view(vk::ImageAspectFlagBits::eColor);
    }
};
