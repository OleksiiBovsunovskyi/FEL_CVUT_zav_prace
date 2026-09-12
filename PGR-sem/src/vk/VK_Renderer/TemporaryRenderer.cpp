module;

#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>

module TemporaryRenderer;

import Logger;

bool TemporaryRenderer::init(VulkanContext& ctx, vk::Extent2D extent) {
    for (auto& depth : depth_) {
        depth.emplace(ctx.allocator(), extent);
        if (!depth->create()) {
            logError("TemporaryRenderer::init: depth target allocation failed");
            return false;
        }
    }
    return true;
}

void TemporaryRenderer::destroy() {
    for (auto& depth : depth_) depth.reset();
}

bool TemporaryRenderer::resize(vk::Extent2D extent) {
    for (auto& depth : depth_) {
        if (!depth || !depth->resize(extent)) {
            logError("TemporaryRenderer::resize: depth target reallocation failed");
            return false;
        }
    }
    return true;
}

void TemporaryRenderer::render(Frame::Recording& recording) {
    const vk::CommandBuffer cmd = recording.commandBuffer();
    DepthRenderTarget& depth = *recording.select(depth_);

    /* UNDEFINED: depth is cleared every frame. */
    transitionImage(cmd, depth.image().handle(),
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eTopOfPipe, {},
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::ImageAspectFlagBits::eDepth);

    vk::RenderingAttachmentInfo depthAttachment{};
    depthAttachment.imageView   = depth.view().get();
    depthAttachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthAttachment.loadOp      = vk::AttachmentLoadOp::eClear;
    // Nothing reads depth back yet, so it need not survive the pass.
    // (TODO: pp, and potentially other renderers will need depth + potentially last frame depth)
    depthAttachment.storeOp     = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.clearValue.depthStencil.depth = DEPTH_CLEAR;

    const vk::RenderingAttachmentInfo colorAttachment =
        recording.colorAttachment(vk::ClearColorValue{std::array<float, 4>{0.2f, 0.1f, 0.3f, 1.0f}});
    vk::RenderingInfo rendering{};
    rendering.renderArea = vk::Rect2D{{0, 0}, recording.extent()};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colorAttachment;
    rendering.pDepthAttachment = &depthAttachment;

    cmd.beginRendering(rendering);
    if (draw_) draw_(cmd, recording.extent());
    cmd.endRendering();
}
