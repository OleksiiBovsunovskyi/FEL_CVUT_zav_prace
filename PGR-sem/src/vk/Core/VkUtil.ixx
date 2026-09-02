module;
#include <vulkan/vulkan.h>

#include <span>
#include <utility>
#include <vector>

export module VkUtil;

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
export constexpr VkCompareOp DEPTH_COMPARE_OP = VK_COMPARE_OP_GREATER_OR_EQUAL;

export constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

static_assert(DEPTH_CLEAR >= 0.0f && DEPTH_CLEAR <= 1.0f,
              "VkClearDepthStencilValue::depth must be within [0,1] unless "
              "VK_EXT_depth_range_unrestricted is enabled; the validation "
              "layers do not report this");

static_assert((DEPTH_COMPARE_OP == VK_COMPARE_OP_GREATER ||
               DEPTH_COMPARE_OP == VK_COMPARE_OP_GREATER_OR_EQUAL)
                  == (DEPTH_CLEAR == 0.0f),
              "DEPTH_CLEAR must be the far plane - 0 for a GREATER compare op, "
              "1 for a LESS one - or every fragment passes the depth test");

static_assert(DEPTH_FORMAT == VK_FORMAT_D32_SFLOAT ||
              DEPTH_FORMAT == VK_FORMAT_D32_SFLOAT_S8_UINT,
              "reverse-Z needs a float depth format; a UNORM one distributes "
              "precision evenly and gains nothing from the flip");


export struct RenderTarget {
    VkImage     image  = VK_NULL_HANDLE;
    VkImageView view   = VK_NULL_HANDLE;
    VkFormat    format = VK_FORMAT_UNDEFINED;
    VkExtent2D  extent{};

    /**
     * Already in DEPTH_ATTACHMENT_OPTIMAL. Optional; a pass without depth omits
     * it and builds its pipeline with depthAttachmentFormat = UNDEFINED.
     */
    VkImageView depthView   = VK_NULL_HANDLE;
    VkFormat    depthFormat = VK_FORMAT_UNDEFINED;
};

/// synchronization2 image layout transition.
export inline void transitionImage(VkCommandBuffer cmd, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout,
                                   VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask  = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout     = oldLayout;
    barrier.newLayout     = newLayout;
    barrier.image         = image;
    barrier.subresourceRange = VkImageSubresourceRange{ aspect, 0, 1, 0, 1 };

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd, &dep);
}

/// synchronization2 barrier covering every buffer write in the given stages.
export inline void barrier(VkCommandBuffer cmd,
                           VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 memoryBarrier{};
    memoryBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memoryBarrier.srcStageMask  = srcStage;
    memoryBarrier.srcAccessMask = srcAccess;
    memoryBarrier.dstStageMask  = dstStage;
    memoryBarrier.dstAccessMask = dstAccess;

    VkDependencyInfo dep{};
    dep.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers    = &memoryBarrier;

    vkCmdPipelineBarrier2(cmd, &dep);
}

/// The same barrier narrowed to specific ranges.
export inline void barrier(VkCommandBuffer cmd,
                           std::span<const BufferRegion> regions,
                           VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    if (regions.empty()) return;

    std::vector<VkBufferMemoryBarrier2> barriers;
    barriers.reserve(regions.size());
    for (const BufferRegion& region : regions) {
        if (!region) continue;

        VkBufferMemoryBarrier2 entry{};
        entry.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        entry.srcStageMask        = srcStage;
        entry.srcAccessMask       = srcAccess;
        entry.dstStageMask        = dstStage;
        entry.dstAccessMask       = dstAccess;
        entry.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        entry.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        entry.buffer              = region.buffer;
        entry.offset              = region.offset;
        entry.size                = region.size;
        barriers.push_back(entry);
    }
    if (barriers.empty()) return;

    VkDependencyInfo dep{};
    dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pBufferMemoryBarriers    = barriers.data();

    vkCmdPipelineBarrier2(cmd, &dep);
}

/**
 * Records a copy of the whole source range. The sizes must match; a mismatch
 * is a caller bug, so nothing is recorded.
 */
export inline void copy(VkCommandBuffer cmd, const BufferRegion& src,
                        const BufferRegion& dst) {
    if (!src || !dst || src.size != dst.size) return;

    const VkBufferCopy region{src.offset, dst.offset, src.size};
    vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &region);
}

/// Fills a range with zeroes. The offset and size must be 4-byte aligned.
export inline void zero(VkCommandBuffer cmd, const BufferRegion& region) {
    if (!region) return;
    vkCmdFillBuffer(cmd, region.buffer, region.offset, region.size, 0);
}

/// Full-target viewport and scissor, in the reverse-Z depth range.
export inline std::pair<VkViewport, VkRect2D> viewportAndScissor(
    VkExtent2D extent) {
    const VkViewport viewport{
        .x = 0.0f, .y = 0.0f,
        .width    = static_cast<float>(extent.width),
        .height   = static_cast<float>(extent.height),
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    return {viewport, VkRect2D{{0, 0}, extent}};
}
