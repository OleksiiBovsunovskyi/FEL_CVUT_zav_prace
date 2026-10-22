export module depthImageDesc;

import vulkan;
import Image;
import RendererConfig;


export constexpr ImageDesc depthDesc(vk::Extent2D extent) {
    return ImageDesc{.format = DEPTH_FORMAT,
                     .extent = vk::Extent3D{extent.width, extent.height, 1},
                     .usage  = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                               vk::ImageUsageFlagBits::eSampled};
}
