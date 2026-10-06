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

vk::RenderingAttachmentInfo SharedRenderTargets::depthAttachmentLoad(
    Frame::Recording& recording) {
    vk::RenderingAttachmentInfo attachment{};
    attachment.imageView   = recording.select(depth_)->view().get();
    attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
    attachment.loadOp      = vk::AttachmentLoadOp::eLoad;
    attachment.storeOp     = vk::AttachmentStoreOp::eStore;
    return attachment;
}

const DepthRenderTarget& SharedRenderTargets::depth(Frame::Recording& recording) {
    return *recording.select(depth_);
}

bool Renderer::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                    vk::Format colorFormat, vk::Extent2D extent,
                    const TextureManager& textures) {
    textures_ = &textures;
    if (!meshDrawResources_.init(ctx, shaderLoader,
                                 SHADER_DIR / "scatter_instances.spv",
                                 SHADER_DIR / "build_draw_commands.spv"))
        return false;

    if (!frameRecord_.init(ctx, "frame")) {
        meshDrawResources_.destroy();
        return false;
    }

    if (!sharedTargets_.init(ctx, extent)) {
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }
    if (!depthPyramid_.init(ctx, shaderLoader, SHADER_DIR / "depth_pyramid.spv", extent)) {
        sharedTargets_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }
    //TODO: Add mesh shader override.
    if (!forward_.init(ctx, shaderLoader, SHADER_DIR / "mesh.spv",
                       SHADER_DIR / "mesh_frag.spv", colorFormat, extent,
                       textures.layout())) {
        depthPyramid_.destroy();
        sharedTargets_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }

    if (!drawStats_.init(ctx)) {
        forward_.destroy();
        depthPyramid_.destroy();
        sharedTargets_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }

    if (!timings_.init(ctx)) logError("Renderer: GPU pass timings unavailable");
    return true;
}

void Renderer::destroy() {
    timings_.destroy();
    drawStats_.destroy();
    forward_.destroy();
    depthPyramid_.destroy();
    sharedTargets_.destroy();
    frameRecord_.destroy();
    meshDrawResources_.destroy();
}

void Renderer::setDrawCallback(DrawFn cb) {
    draw_ = std::move(cb);
}

void Renderer::render(Frame::Recording& recording,
                      std::span<const GPUMeshInstance> instances,
                      std::span<const uint32_t> changed,
                      std::span<const uint32_t> meshletCounts,
                      const glm::mat4& viewProjection,
                      const glm::vec3& cameraPosition,
                      GpuPtr<GPUMaterial> materials) {
    timings_.beginFrame(recording);

    const GpuPtr<GPUFrameData> frame = frameRecord_.write(
        recording.frameInFlight(),
        GPUFrameData{
            .camera       = GPUCameraData{viewProjection, glm::vec4(cameraPosition, 1.0f)},
            .drawCounters = drawStats_.beginFrame(recording, static_cast<uint32_t>(instances.size())),
            .debugShowMeshlets = debugFlags_.ShowMeshlets,
            .debugShowMeshletSpheres = debugFlags_.ShowMeshletSpheres,
            .debugDrawNormals = debugFlags_.DrawNormals,
            .debugDisableOcclusion = debugFlags_.DisableOcclusion,
        });
    const PreparedMeshDraw meshDraw =
        meshDrawResources_.prepare(recording, instances, changed, meshletCounts, frame, timings_);
    const vk::DescriptorSet textureSet = textures_ ? textures_->set() : nullptr;

    /* A single pass carries the draw callback: nothing to cull, or occlusion disabled. */
    const bool twoPass = meshDraw && !debugFlags_.DisableOcclusion;
    forward_.render(recording, meshDraw, frame, materials, textureSet,
                    sharedTargets_.depthAttachment(recording),
                    twoPass ? DrawFn{} : draw_, timings_);

    if (twoPass) {
        const DepthRenderTarget& depth = sharedTargets_.depth(recording);
        const DepthPyramidView hiZ = depthPyramid_.build(
            recording, depth.image().handle(), depth.extent2D(), timings_);
        const PreparedMeshDraw secondPass =
            meshDrawResources_.cullSecondPass(recording, meshDraw, frame, hiZ, timings_);
        forward_.render(recording, secondPass, frame, materials, textureSet,
                        sharedTargets_.depthAttachmentLoad(recording), draw_, timings_,
                        true);
    }
    drawStats_.endFrame(recording);
}

bool Renderer::resize(vk::Extent2D extent) {
    if (!sharedTargets_.resize(extent)) return false;
    if (!depthPyramid_.resize(extent)) return false;
    return forward_.resize(extent);
}
