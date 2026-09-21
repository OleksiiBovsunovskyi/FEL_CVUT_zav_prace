module;

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

export module VkUtil;

import vulkan;
import VK_Buffers;

/**
 * Vocabulary shared by the frame loop and every renderer.
 */

/**
 * VK depth range is [0;1], used inverted: 0 is far, 1 is near.
 *
 * The asserts below cover the clear value and the compare op. The projection's
 * swapped near/far is the third part of the convention and stays caller-side,
 * unchecked.
 */
export constexpr float DEPTH_CLEAR = 0.0f;
export constexpr vk::CompareOp DEPTH_COMPARE_OP = vk::CompareOp::eGreaterOrEqual;

export constexpr vk::Format DEPTH_FORMAT = vk::Format::eD32Sfloat;

static_assert(DEPTH_CLEAR >= 0.0f && DEPTH_CLEAR <= 1.0f,
              "vk::ClearDepthStencilValue::depth must be within [0,1] unless "
              "VK_EXT_depth_range_unrestricted is enabled; the validation "
              "layers do not report this");

static_assert((DEPTH_COMPARE_OP == vk::CompareOp::eGreater ||
               DEPTH_COMPARE_OP == vk::CompareOp::eGreaterOrEqual)
                  == (DEPTH_CLEAR == 0.0f),
              "DEPTH_CLEAR must be the far plane - 0 for a GREATER compare op, "
              "1 for a LESS one - or every fragment passes the depth test");

static_assert(DEPTH_FORMAT == vk::Format::eD32Sfloat ||
              DEPTH_FORMAT == vk::Format::eD32SfloatS8Uint,
              "reverse-Z needs a float depth format; a UNORM one distributes "
              "precision evenly and gains nothing from the flip");

/// synchronization2 image layout transition.
export inline void transitionImage(vk::CommandBuffer cmd, vk::Image image,
                                   vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                   vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
                                   vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess,
                                   vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
                                   uint32_t baseMipLevel = 0, uint32_t levelCount = 1) {
    vk::ImageMemoryBarrier2 barrier{};
    barrier.srcStageMask  = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask  = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout     = oldLayout;
    barrier.newLayout     = newLayout;
    barrier.image         = image;
    barrier.subresourceRange =
        vk::ImageSubresourceRange{ aspect, baseMipLevel, levelCount, 0, 1 };

    vk::DependencyInfo dep{};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;

    cmd.pipelineBarrier2(dep);
}

/// synchronization2 barrier covering every buffer write in the given stages.
export inline void barrier(vk::CommandBuffer cmd,
                           vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
                           vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess) {
    vk::MemoryBarrier2 memoryBarrier{};
    memoryBarrier.srcStageMask  = srcStage;
    memoryBarrier.srcAccessMask = srcAccess;
    memoryBarrier.dstStageMask  = dstStage;
    memoryBarrier.dstAccessMask = dstAccess;

    vk::DependencyInfo dep{};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers    = &memoryBarrier;

    cmd.pipelineBarrier2(dep);
}

/// The same barrier narrowed to specific ranges.
export inline void barrier(vk::CommandBuffer cmd,
                           std::span<const BufferRegion> regions,
                           vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
                           vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess) {
    if (regions.empty()) return;

    std::vector<vk::BufferMemoryBarrier2> barriers;
    barriers.reserve(regions.size());
    for (const BufferRegion& region : regions) {
        if (!region) continue;

        vk::BufferMemoryBarrier2 entry{};
        entry.srcStageMask        = srcStage;
        entry.srcAccessMask       = srcAccess;
        entry.dstStageMask        = dstStage;
        entry.dstAccessMask       = dstAccess;
        entry.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        entry.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        entry.buffer              = region.buffer;
        entry.offset              = region.offset;
        entry.size                = region.size;
        barriers.push_back(entry);
    }
    if (barriers.empty()) return;

    vk::DependencyInfo dep{};
    dep.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pBufferMemoryBarriers    = barriers.data();

    cmd.pipelineBarrier2(dep);
}

/**
 * Records a copy of the whole source range. The sizes must match; a mismatch
 * is a caller bug, so nothing is recorded.
 */
export inline void copy(vk::CommandBuffer cmd, const BufferRegion& src,
                        const BufferRegion& dst) {
    if (!src || !dst || src.size != dst.size) return;

    const vk::BufferCopy region{src.offset, dst.offset, src.size};
    cmd.copyBuffer(src.buffer, dst.buffer, 1, &region);
}

/// Fills a range with zeroes. The offset and size must be 4-byte aligned.
export inline void zero(vk::CommandBuffer cmd, const BufferRegion& region) {
    if (!region) return;
    cmd.fillBuffer(region.buffer, region.offset, region.size, 0);
}

/// Full-target viewport and scissor, in the reverse-Z depth range.
export inline std::pair<vk::Viewport, vk::Rect2D> viewportAndScissor(
    vk::Extent2D extent) {
    const vk::Viewport viewport{
        0.0f, 0.0f,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0f, 1.0f,
    };
    return {viewport, vk::Rect2D{{0, 0}, extent}};
}
