export module RendererConfig;

import vulkan;

/// Format of every depth attachment.
export constexpr vk::Format DEPTH_FORMAT = vk::Format::eD32Sfloat;

static_assert(DEPTH_FORMAT == vk::Format::eD32Sfloat ||
              DEPTH_FORMAT == vk::Format::eD32SfloatS8Uint,
              "reverse-Z needs a float depth format; a UNORM one distributes "
              "precision evenly and gains nothing from the flip");
