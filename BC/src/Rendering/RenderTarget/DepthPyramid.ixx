module;
#include <stdint.h>

export module renderTarget:DepthPyramid;
import :ColorRenderTarget;
import vulkan;
import vk_mem_alloc;
/**
 * TODO: Come up with explanation what is it.
 */
export class DepthPyramid : public ColorRenderTarget
{
public:
    DepthPyramid(vma::Allocator allocator, vk::Extent2D extent)
            : ColorRenderTarget(allocator, vk::Format::eR32Sfloat, extent,
                                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                                mipCountFor(vk::Extent3D{extent.width, extent.height, 1})) {}

    /// Levels in a full chain for an extent up to 65535 on either side.
    static constexpr uint32_t MAX_MIP_LEVELS = 16;

    /// @return levels in a full chain for a pyramid of this extent.
    [[nodiscard]] static constexpr uint32_t mipCountFor(vk::Extent3D extent) {
        return maxMipLevels(extent);
    }
    [[nodiscard]] ImageRef mipView(uint32_t level) {
        return mutableImage().view(
            vk::ImageViewType::e2D,
            format(),
             {vk::ImageAspectFlagBits::eColor, level,1, 0, 1});
    }
protected:
    [[nodiscard]] uint32_t mipLevelsFor(vk::Extent3D extent) const override {
        return mipCountFor(extent);
    }
};
 