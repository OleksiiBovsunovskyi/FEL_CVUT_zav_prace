module;

#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <filesystem>

#include <glm/glm.hpp>

module TemporaryRenderer;

import Logger;

bool TemporaryRenderer::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                             const std::filesystem::path& meshShaderPath,
                             const std::filesystem::path& fragmentShaderPath,
                             vk::Format colorFormat, vk::Extent2D extent) {
    for (auto& depth : depth_) {
        depth.emplace(ctx.allocator(), extent);
        if (!depth->create()) {
            logError("TemporaryRenderer::init: depth target allocation failed");
            destroy();
            return false;
        }
    }
    if (!meshDraw_.init(ctx.device(), shaderLoader, meshShaderPath, fragmentShaderPath,
                        colorFormat, DEPTH_FORMAT, ctx.cmdDrawMeshTasksIndirectCount())) {
        destroy();
        return false;
    }
    return true;
}

void TemporaryRenderer::destroy() {
    meshDraw_.destroy();
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

void TemporaryRenderer::render(Frame::Recording& recording,
                               const PreparedMeshDraw& preparedMeshDraw,
                               const glm::mat4& viewProjection,
                               GpuPtr<GPUMaterial> materials) {
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
    if (preparedMeshDraw) {
        GPUMeshDrawPush push{};
        push.viewProj = viewProjection;
        push.drawData = preparedMeshDraw.drawData;
        push.instances = preparedMeshDraw.instances;
        push.materials = materials;
        meshDraw_.record(cmd, recording.extent(), push,
                         preparedMeshDraw.indirectCommands,
                         preparedMeshDraw.indirectCount,
                         preparedMeshDraw.instanceCount);
    }
    if (draw_) draw_(cmd, recording.extent());
    cmd.endRendering();
}
