module;

#include <cstdint>

export module renderTarget:RenderTarget2D;

import vulkan;
import vk_mem_alloc;
export import :RenderTarget;
import Image;

/**
 * A render target of one 2D image with one array layer.
 */
export class RenderTarget2D : public RenderTarget {
public:
    /**
     * @param allocator VMA allocator the image is allocated from.
     * @param format texel format.
     * @param extent size in texels; both members must be at least 1.
     * @param usage every way the image will ever be used.
     * @param mipLevels levels in the chain.
     * @param samples samples per texel; every attachment in one pass must agree.
     */
    RenderTarget2D(vma::Allocator allocator, vk::Format format, vk::Extent2D extent,
                   vk::ImageUsageFlags usage, uint32_t mipLevels = 1,
                   vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
        : RenderTarget(allocator, vk::ImageType::e2D, format,
                       vk::Extent3D{extent.width, extent.height, 1}, usage,
                       mipLevels, 1, samples) {}

    /**
     * @param device device the views are created on.
     * @param image image to borrow.
     * @param format texel format.
     * @param extent size in texels; both members must be at least 1.
     * @param mipLevels levels in the chain.
     * @param samples samples per texel.
     * @note Borrowed image will not be owned by this class.
     */
    RenderTarget2D(vk::Device device, vk::Image image, vk::Format format, vk::Extent2D extent,
                   uint32_t mipLevels = 1,
                   vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1)
        : RenderTarget(device, image, vk::ImageType::e2D, format,
                       vk::Extent3D{extent.width, extent.height, 1},
                       mipLevels, 1, samples) {}

    /**
     * Borrows an existing vk::Image, replacing whatever was held.
     *
     * @param device device the views are created on.
     * @param image image to borrow.
     * @param format texel format.
     * @param extent size in texels; both members must be at least 1.
     * @param mipLevels levels in the chain.
     * @param samples samples per texel.
     * @note Borrowed image will not be owned by this class.
     */
    void borrow(vk::Device device, vk::Image image, vk::Format format, vk::Extent2D extent,
                uint32_t mipLevels = 1,
                vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1) {
        RenderTarget::borrow(device, image, vk::ImageType::e2D, format,
                             vk::Extent3D{extent.width, extent.height, 1},
                             mipLevels, 1, samples);
    }

    /**
     * Resizes the render target, recreating the image and invalidating every view of it.
     * @note The GPU must be done with the image before this is called.
     */
    [[nodiscard]] bool resize(vk::Extent2D extent) {
        return RenderTarget::resize(vk::Extent3D{extent.width, extent.height, 1});
    }

    /// @return size in texels.
    [[nodiscard]] vk::Extent2D extent2D() const {
        return vk::Extent2D{RenderTarget::extent().width, RenderTarget::extent().height};
    }
};

