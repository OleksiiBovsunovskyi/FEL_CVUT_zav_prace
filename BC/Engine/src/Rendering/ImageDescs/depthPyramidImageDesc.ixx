export module depthPyramidImageDesc;

import vulkan;
import Image;

/**
 * @param extent size in texels of the depth the pyramid is built from.
 * @return parameters of a depth pyramid with a full mip chain.
 */
export constexpr ImageDesc depthPyramidDesc(vk::Extent2D extent) {
    const vk::Extent3D extent3D{extent.width, extent.height, 1};
    return ImageDesc{.format    = vk::Format::eR32Sfloat,
                     .extent    = extent3D,
                     .usage     = vk::ImageUsageFlagBits::eStorage |
                                  vk::ImageUsageFlagBits::eSampled,
                     .mipLevels = maxMipLevels(extent3D)};
}
