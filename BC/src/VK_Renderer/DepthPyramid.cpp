module;

#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>

module DepthPyramid;

import vulkan;
import Logger;
import VkUtil;

namespace {

constexpr vk::BufferUsageFlags PYRAMID_USAGE = GPU_DATA_USAGE;

constexpr BufferAccess TRANSFER_WRITE{vk::PipelineStageFlagBits2::eAllTransfer,
                                      vk::AccessFlagBits2::eTransferWrite};
constexpr BufferAccess COMPUTE_WRITE{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead |
        vk::AccessFlagBits2::eShaderStorageWrite};
constexpr BufferAccess COMPUTE_READ{vk::PipelineStageFlagBits2::eComputeShader,
                                    vk::AccessFlagBits2::eShaderStorageRead};

/// @return levels in a chain that halves the extent down to 1x1.
uint32_t levelCount(vk::Extent2D extent) {
    return static_cast<uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
}

/// @return size of a level in texels; matches levelOffset() in HiZOcclusion.slang.
vk::Extent2D levelExtent(vk::Extent2D extent, uint32_t level) {
    return {std::max(extent.width >> level, 1u), std::max(extent.height >> level, 1u)};
}

/// @return texels before a level.
uint64_t levelOffset(vk::Extent2D extent, uint32_t level) {
    uint64_t offset = 0;
    for (uint32_t i = 0; i < level; ++i) {
        const vk::Extent2D size = levelExtent(extent, i);
        offset += uint64_t{size.width} * size.height;
    }
    return offset;
}

void keep(std::vector<vk::BufferMemoryBarrier2>& barriers,
          std::optional<vk::BufferMemoryBarrier2> barrier) {
    if (barrier) barriers.push_back(*barrier);
}

} // namespace

bool DepthPyramid::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                        const std::filesystem::path& reduceShaderPath,
                        vk::Extent2D extent) {
    ctx_ = &ctx;
    if (!reduce_.init(ctx.device(), shaderLoader, reduceShaderPath)) {
        destroy();
        return false;
    }
    if (!resize(extent)) {
        destroy();
        return false;
    }
    return true;
}

void DepthPyramid::destroy() {
    texels_.destroy();
    reduce_.destroy();
    extent_ = vk::Extent2D{};
    levels_ = 0;
    ctx_    = nullptr;
}

bool DepthPyramid::resize(vk::Extent2D extent) {
    if (texels_ && extent == extent_) return true;
    if (!ctx_ || extent.width == 0 || extent.height == 0) return false;

    texels_.destroy();
    extent_ = vk::Extent2D{};
    levels_ = 0;

    const uint32_t levels = levelCount(extent);
    const uint64_t texels = levelOffset(extent, levels);
    if (!texels_.init(*ctx_, texels * sizeof(GPUHiZTexel), PYRAMID_USAGE,
                      "depth pyramid")) {
        logError("DepthPyramid::resize: allocation failed");
        return false;
    }
    extent_ = extent;
    levels_ = levels;
    return true;
}

DepthPyramidView DepthPyramid::build(Frame::Recording& recording, vk::Image depth,
                                     vk::Extent2D depthExtent,
                                     GpuPassTimings& timings) {
    if (!texels_ || depthExtent != extent_) return {};

    const vk::CommandBuffer cmd = recording.commandBuffer();

    transitionImage(cmd, depth, vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::ImageAspectFlagBits::eDepth);

    barriers_.clear();
    keep(barriers_, texels_.use(TRANSFER_WRITE));
    recordBarriers(cmd, barriers_);
    barriers_.clear();

    vk::BufferImageCopy copy{};
    copy.imageSubresource = vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eDepth, 0, 0, 1};
    copy.imageExtent      = vk::Extent3D{extent_.width, extent_.height, 1};
    cmd.copyImageToBuffer(depth, vk::ImageLayout::eTransferSrcOptimal,
                          texels_.region().buffer, 1, &copy);

    transitionImage(cmd, depth, vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eCopy, {},
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::ImageAspectFlagBits::eDepth);

    for (uint32_t level = 1; level < levels_; ++level) {
        keep(barriers_, texels_.use(COMPUTE_WRITE));
        recordBarriers(cmd, barriers_);
        barriers_.clear();

        const vk::Extent2D source      = levelExtent(extent_, level - 1);
        const vk::Extent2D destination = levelExtent(extent_, level);

        DepthPyramidReducePush push{};
        push.source = texels_.gpuAddress<GPUHiZTexel>(
            levelOffset(extent_, level - 1) * sizeof(GPUHiZTexel));
        push.destination = texels_.gpuAddress<GPUHiZTexel>(
            levelOffset(extent_, level) * sizeof(GPUHiZTexel));
        push.sourceWidth       = source.width;
        push.sourceHeight      = source.height;
        reduce_.record(cmd, push, destination.width * destination.height);
    }

    keep(barriers_, texels_.use(COMPUTE_READ));
    recordBarriers(cmd, barriers_);
    barriers_.clear();
    timings.mark(cmd, "depth pyramid");

    return DepthPyramidView{texels_.gpuAddress<GPUHiZTexel>(), extent_, levels_};
}
