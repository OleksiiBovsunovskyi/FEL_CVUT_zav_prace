module;

#include <filesystem>
#include <span>
#include <utility>

#include <glm/glm.hpp>

module Renderer;

import vulkan;
import Logger;
import VkUtil;

namespace {

const std::filesystem::path SHADER_DIR{"Shaders"};

} // namespace

bool SharedRenderTargets::init(VulkanContext& ctx, vk::Extent2D extent) {
    for (auto& depth : depth_) {
        depth.emplace(ctx.allocator(), extent);
        if (!depth->create()) {
            logError("SharedRenderTargets::init: depth target allocation failed");
            destroy();
            return false;
        }
    }
    return true;
}

void SharedRenderTargets::destroy() {
    for (auto& depth : depth_) depth.reset();
}

bool SharedRenderTargets::resize(vk::Extent2D extent) {
    for (auto& depth : depth_) {
        if (!depth || !depth->resize(extent)) {
            logError("SharedRenderTargets::resize: depth target reallocation failed");
            return false;
        }
    }
    return true;
}

vk::RenderingAttachmentInfo SharedRenderTargets::depthAttachment(
    Frame::Recording& recording) {
    const vk::CommandBuffer cmd = recording.commandBuffer();
    DepthRenderTarget& depth = *recording.select(depth_);
    //TODO: move those flags to DepthRenderTarget??? Evaluate potential uses of DepthRenderTarget.
    transitionImage(cmd, depth.image().handle(), vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eDepthAttachmentOptimal,
                    vk::PipelineStageFlagBits2::eTopOfPipe, {},
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                        vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::ImageAspectFlagBits::eDepth);

    vk::RenderingAttachmentInfo attachment{};
    attachment.imageView   = depth.view().get();
    attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    attachment.loadOp      = vk::AttachmentLoadOp::eClear;
    attachment.storeOp     = vk::AttachmentStoreOp::eStore;
    attachment.clearValue.depthStencil.depth = DEPTH_CLEAR;
    return attachment;
}

bool Renderer::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                    vk::Format colorFormat, vk::Extent2D extent,
                    const TextureManager& textures) {
    textures_ = &textures;
    if (!meshDrawResources_.init(ctx, shaderLoader,
                                 SHADER_DIR / "scatter_instances.spv",
                                 SHADER_DIR / "build_draw_commands.spv"))
        return false;

    if (!cameraPerFrameRecord_.init(ctx, "camera")) {
        meshDrawResources_.destroy();
        return false;
    }

    if (!sharedTargets_.init(ctx, extent)) {
        cameraPerFrameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }
    //TODO: Add mesh shader override. 
    if (!forward_.init(ctx, shaderLoader, SHADER_DIR / "mesh.spv",
                       SHADER_DIR / "mesh_frag.spv", colorFormat, extent,
                       textures.layout())) {
        sharedTargets_.destroy();
        cameraPerFrameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }

    if (!timings_.init(ctx)) logError("Renderer: GPU pass timings unavailable");
    return true;
}

void Renderer::destroy() {
    timings_.destroy();
    forward_.destroy();
    sharedTargets_.destroy();
    cameraPerFrameRecord_.destroy();
    meshDrawResources_.destroy();
}

void Renderer::setDrawCallback(DrawFn cb) {
    draw_ = std::move(cb);
}

void Renderer::render(Frame::Recording& recording,
                      std::span<const GPUMeshInstance> instances,
                      std::span<const uint32_t> changed,
                      const glm::mat4& viewProjection,
                      const glm::vec3& cameraPosition,
                      GpuPtr<GPUMaterial> materials) {
    timings_.beginFrame(recording);

    //Upload camera data
    const GpuPtr<GPUCameraData> camera = cameraPerFrameRecord_.write(
        recording.frameInFlight(),
        GPUCameraData{viewProjection, glm::vec4(cameraPosition, 1.0f)});
    const PreparedMeshDraw meshDraw =
        meshDrawResources_.prepare(recording, instances, changed, camera, timings_);
    const vk::RenderingAttachmentInfo depthAttachment =
        sharedTargets_.depthAttachment(recording);
    forward_.render(recording, meshDraw, camera, materials,
                    textures_ ? textures_->set() : nullptr, depthAttachment, draw_,
                    timings_);
}

bool Renderer::resize(vk::Extent2D extent) {
    if (!sharedTargets_.resize(extent)) return false;
    return forward_.resize(extent);
}
