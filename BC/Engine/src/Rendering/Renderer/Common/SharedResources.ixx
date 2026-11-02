module;

#include <array>
#include <cstdint>

export module SharedResources;

import vulkan;
import Logger;
import Frame;
import VulkanContext;
import Image;
import TextureManager;
import RegisteredImage;
import RendererConfig;
import VkUtil;
import depthImageDesc;
import depthPyramidImageDesc;
import VisibilityBufferImageDesc;

/// Depth target, sampled by build_depth_pyramid.
inline constexpr RegisteredImageParameters DEPTH_REGISTRATION{
    .imageAspect = vk::ImageAspectFlagBits::eDepth,
    .imageLayout = SAMPLED_DEPTH_LAYOUT};

/// Depth pyramid, sampled for occlusion culling and written per mip by build_depth_pyramid.
inline constexpr RegisteredImageParameters DEPTH_PYRAMID_REGISTRATION{
    .imageAspect = vk::ImageAspectFlagBits::eColor,
    .imageLayout = DEPTH_PYRAMID_LAYOUT,
    .storageMips = true};

/// Visibility target, sampled by the deferred draw pass.
inline constexpr RegisteredImageParameters VISIBILITY_REGISTRATION{
    .imageAspect = vk::ImageAspectFlagBits::eColor,
    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

/**
 * Owns the render targets shared by the forward and deferred renderers.
 * Depth: depth attachment, sampled by build_depth_pyramid.
 * Depth pyramid: built from depth, holds the previous frame's depth for occlusion culling.
 * Visibility: visibility attachment of the deferred renderer.
 */
export class SharedResources {
public:
    /**
     * Allocates every target and registers it with the texture manager.
     * @param textures texture manager the targets are registered in.
     * @return false after logging when an allocation or registration failed.
     */
    [[nodiscard]] bool init(const VulkanContext& ctx, const vk::Extent2D extent, TextureManager& textures) {
        if (!depth_.allocate({ctx.allocator(), textures, depthDesc(extent), DEPTH_REGISTRATION})) {
            logError("SharedResources: depth allocation failed");
            destroy();
            return false;
        }
        if (!depthPyramid_.allocate({ctx.allocator(), textures, depthPyramidDesc(extent),
                                     DEPTH_PYRAMID_REGISTRATION})) {
            logError("SharedResources: depth pyramid allocation failed");
            destroy();
            return false;
        }
        if (!visibility_.allocate({ctx.allocator(), textures, visibilityBufferDesc(extent),
                                   VISIBILITY_REGISTRATION})) {
            logError("SharedResources: visibility allocation failed");
            destroy();
            return false;
        }
        return true;
    }

    /**
     * Retires every slot and destroys every target. Safe to call twice.
     * @note The GPU must be done with the targets before this is called.
     */
    void destroy() {
        depth_.destroy();
        depthPyramid_.destroy();
        visibility_.destroy();
    }

    /**
     * Reallocates every target at the new extent, keeping their slots.
     * @note The GPU must be done with the targets before this is called.
     */
    [[nodiscard]] bool resize(vk::Extent2D extent) {
        if (!depth_.reallocate(depthDesc(extent))) {
            logError("SharedResources: depth resize failed");
            return false;
        }
        if (!depthPyramid_.reallocate(depthPyramidDesc(extent))) {
            logError("SharedResources: depth pyramid resize failed");
            return false;
        }
        if (!visibility_.reallocate(visibilityBufferDesc(extent))) {
            logError("SharedResources: visibility resize failed");
            return false;
        }
        return true;
    }

    /**
     * Transitions depth to DEPTH_ATTACHMENT_READ_WRITE.
     * @return depth attachment, cleared to DEPTH_CLEAR.
     */
    [[nodiscard]] vk::RenderingAttachmentInfo getDepthAttachment(const Frame::Recording& recording) {
        depth_.image().use(recording.commandBuffer(), DEPTH_ATTACHMENT_READ_WRITE);
        vk::RenderingAttachmentInfo attachment{};
        attachment.imageView   = depth_.image().view(vk::ImageAspectFlagBits::eDepth).get();
        attachment.imageLayout = DEPTH_ATTACHMENT_READ_WRITE.layout;
        attachment.loadOp      = vk::AttachmentLoadOp::eClear;
        attachment.storeOp     = vk::AttachmentStoreOp::eStore;
        attachment.clearValue.depthStencil.depth = DEPTH_CLEAR;
        return attachment;
    }

    /**
     * Transitions visibility to VISIBILITY_ATTACHMENT_WRITE.
     * @return visibility attachment, cleared to VISIBILITY_CLEAR.
     */
    [[nodiscard]] vk::RenderingAttachmentInfo getVisibilityAttachment (
        const Frame::Recording& recording) {
        visibility_.image().use(recording.commandBuffer(), VISIBILITY_ATTACHMENT_WRITE);
        vk::RenderingAttachmentInfo attachment{};
        attachment.imageView   = visibility_.image().view(vk::ImageAspectFlagBits::eColor).get();
        attachment.imageLayout = VISIBILITY_ATTACHMENT_WRITE.layout;
        attachment.loadOp      = vk::AttachmentLoadOp::eClear;
        attachment.storeOp     = vk::AttachmentStoreOp::eStore;
        attachment.clearValue  = vk::ClearColorValue{std::array<uint32_t, 4>{VISIBILITY_CLEAR, 0, 0, 0}};
        return attachment;
    }
    /**
     * Transitions depth to DEPTH_SAMPLED_COMPUTE_READ.
     * @return sampled slot of depth.
     */
    [[nodiscard]] TextureSlot getSampledDepthSlot(const Frame::Recording& recording) {
        depth_.image().use(recording.commandBuffer(), DEPTH_SAMPLED_COMPUTE_READ);
        return depth_.sampledSlot();
    }

    [[nodiscard]] RegisteredImage&       getDepthPyramid()       { return depthPyramid_; }
    [[nodiscard]] const RegisteredImage& getDepthPyramid() const { return depthPyramid_; }

    [[nodiscard]] TextureSlot getSampledVisibilitySlot(const Frame::Recording& recording) {
        visibility_.image().use(recording.commandBuffer(), TEXTURE_FRAGMENT_SAMPLED_READ);
        return visibility_.sampledSlot();
    }

private:
    RegisteredImage depth_;
    RegisteredImage depthPyramid_;
    RegisteredImage visibility_;
};
