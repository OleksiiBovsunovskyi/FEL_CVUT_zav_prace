module;

#include <cstdint>

export module renderTarget:DepthRenderTarget;

import vulkan;
import vk_mem_alloc;
export import :RenderTarget2D;
import Image;
import VkUtil;

/**
 * The depth attachment of one pass, in VkUtil's DEPTH_FORMAT.
 */
export class DepthRenderTarget : public RenderTarget2D {
public:
    /**
     * @param allocator VMA allocator the image is allocated from.
     * @param extent size in texels; both members must be at least 1.
     */
    DepthRenderTarget(vma::Allocator allocator, vk::Extent2D extent)
        : RenderTarget2D(allocator, DEPTH_FORMAT, extent,
                         vk::ImageUsageFlagBits::eDepthStencilAttachment |
                             vk::ImageUsageFlagBits::eSampled) {}

    /// @return a view of the depth aspect. Empty when creation failed.
    [[nodiscard]] ImageRef view() {
        return RenderTarget::view(vk::ImageAspectFlagBits::eDepth);
    }
};
