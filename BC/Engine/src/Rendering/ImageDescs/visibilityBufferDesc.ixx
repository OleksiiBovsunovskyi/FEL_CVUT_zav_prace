module;

#include <cstdint>

export module VisibilityByfferImageDesc;

import vulkan;
import Image;

/// Triangle ID of a texel no triangle covers.
export constexpr uint32_t VISIBILITY_CLEAR = 0xFFFFFFFF;

export constexpr ImageDesc visibilityBufferDesc(vk::Extent2D extent) {
    const vk::Extent3D extent3D{extent.width, extent.height, 1};
    return ImageDesc{.format    = vk::Format::eR32Uint,
                     .extent    = extent3D,
                     .usage     = vk::ImageUsageFlagBits::eColorAttachment |
                                  vk::ImageUsageFlagBits::eSampled,
                     .mipLevels = 1};
}
