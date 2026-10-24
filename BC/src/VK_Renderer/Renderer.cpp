module;

#include <algorithm>
#include <filesystem>
#include <span>
#include <utility>

#include <glm/glm.hpp>

module Renderer;

import vulkan;
import Logger;
import VkUtil;
import Image;
import depthImageDesc;
import depthPyramidImageDesc;
import ComputePass;
import Resource;
import RendererConfig;

namespace {

const std::filesystem::path SHADER_DIR{"Shaders"};

/// Pyramid mip 0 texels one build_depth_pyramid workgroup covers per side.
/// Force this agreeing between shader and c++ side
constexpr uint32_t DEPTH_PYRAMID_TILE = 64;

/**
 * @param pyramid pyramid the view is taken from.
 * @param level mip level
 * @return a view of mip `level`, or of the last mip past the chain.
 */
ImageRef pyramidMipView(Image& pyramid, uint32_t level) {
    const uint32_t mip = std::min(level, pyramid.mipLevels() - 1);
    return pyramid.view(vk::ImageViewType::e2D, pyramid.format(),
                        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, mip, 1, 0, 1});
}

} // namespace

bool SharedRenderTargets::init(VulkanContext& ctx, vk::Extent2D extent,
                               TextureManager& textures) {
    textures_ = &textures;
    for (size_t i = 0; i < depth_.size(); ++i) {
        Image& depth = depth_[i];
        if (!depth.allocate(ctx.allocator(), depthDesc(extent))) {
            logError("SharedRenderTargets::init: depth target allocation failed");
            destroy();
            return false;
        }
        depthSlots_[i] = textures.addSampledBorrowed(depth.view(vk::ImageAspectFlagBits::eDepth),
                                                     SAMPLED_DEPTH_LAYOUT);
        if (!depthSlots_[i]) {
            logError("SharedRenderTargets::init: depth target registration failed");
            destroy();
            return false;
        }
    }
    return true;
}

void SharedRenderTargets::destroy() {
    for (Image& depth : depth_) depth.destroy();
}

bool SharedRenderTargets::resize(vk::Extent2D extent) {
    for (size_t i = 0; i < depth_.size(); ++i) {
        Image& depth = depth_[i];
        if (!depth || !depth.reallocate(depthDesc(extent))) {
            logError("SharedRenderTargets::resize: depth target reallocation failed");
            return false;
        }
        if (!textures_->replaceSampledBorrowed(depthSlots_[i],
                                               depth.view(vk::ImageAspectFlagBits::eDepth),
                                               SAMPLED_DEPTH_LAYOUT)) {
            logError("SharedRenderTargets::resize: depth target re-registration failed");
            return false;
        }
    }
    return true;
}

TextureSlot SharedRenderTargets::sampledDepth(Frame::Recording& recording) {
    Image& depth = recording.select(depth_);
    depth.use(recording.commandBuffer(), DEPTH_SAMPLED_COMPUTE_READ);
    return recording.select(depthSlots_);
}

vk::RenderingAttachmentInfo SharedRenderTargets::getDepthAttachment(
    Frame::Recording& recording) {
    const vk::CommandBuffer cmd = recording.commandBuffer();
    Image& depth = recording.select(depth_);
    depth.use(cmd, DEPTH_ATTACHMENT_READ_WRITE);

    vk::RenderingAttachmentInfo attachment{};
    attachment.imageView   = depth.view(vk::ImageAspectFlagBits::eDepth).get();
    attachment.imageLayout = DEPTH_ATTACHMENT_READ_WRITE.layout;
    attachment.loadOp      = vk::AttachmentLoadOp::eClear;
    attachment.storeOp     = vk::AttachmentStoreOp::eStore;
    attachment.clearValue.depthStencil.depth = DEPTH_CLEAR;
    return attachment;
}

bool Renderer::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                    vk::Format colorFormat, vk::Extent2D extent,
                    TextureManager& textures) {
    textures_ = &textures;
    if (!meshDrawResources_.init(ctx, shaderLoader,
                                 SHADER_DIR / "scatter_instances.spv",
                                 SHADER_DIR / "build_draw_commands.spv", textures.layout()))
        return false;

    if (!frameRecord_.init(ctx, "frame")) {
        meshDrawResources_.destroy();
        return false;
    }

    if (!sharedTargets_.init(ctx, extent, textures)) {
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }
    //TODO: Add mesh shader override. 
    if (!forward_.init(ctx, shaderLoader, SHADER_DIR / "mesh.spv",
                       SHADER_DIR / "mesh_frag.spv", colorFormat, extent,
                       textures.layout())) {
        sharedTargets_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }

    if (!drawStats_.init(ctx)) {
        forward_.destroy();
        sharedTargets_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }

    if (!timings_.init(ctx)) logError("Renderer: GPU pass timings unavailable");
    
    //Create depth pyramid
    if (!depthPyramid_.allocate(ctx.allocator(), depthPyramidDesc(extent))) {
        logError("Renderer: depth pyramid allocation failed");
        destroy();
        return false;
    }
    depthPyramidUndefined_ = true;
    //Register with texture manager
    depthPyramidSlot_ = textures_->addSampledBorrowed(
        depthPyramid_.view(vk::ImageAspectFlagBits::eColor), DEPTH_PYRAMID_LAYOUT);
    if (!depthPyramidSlot_) {
        destroy();
        return false;
    }

    if (depthPyramid_.mipLevels() > DEPTH_PYRAMID_MAX_MIP_LEVELS) {
        logError("Renderer: depth pyramid has more mips than its storage slots");
        destroy();
        return false;
    }
    // Create storage slots for each of depth pyramid mips
    for (uint32_t level = 0; level < depthPyramidMipSlots_.size(); ++level) {
        depthPyramidMipSlots_[level] =
            textures_->addStorage(pyramidMipView(depthPyramid_, level));
        if (depthPyramidMipSlots_[level].value != depthPyramidMipSlots_[0].value + level) {
            logError("Renderer: depth pyramid storage slots are not consecutive");
            destroy();
            return false;
        }
    }
    depthPyramidPush_.pyramidMip0Slot = depthPyramidMipSlots_[0].value;
    depthPyramidPush_.mipCount        = depthPyramid_.mipLevels();
    //Prepare depth pyramid build pass
    if (!depthPyramidPass_.init(ctx.device(), shaderLoader, SHADER_DIR / "build_depth_pyramid.spv",
                                textures.layout()))
    {
        logError("Renderer: depth pyramid build pass initialization failed");
        destroy();
        return false;
    }
    if (!depthPyramidCounter_.init(ctx, sizeof(GPUDepthPyramidCounter), GPU_DATA_USAGE,
                                   "depth pyramid counter")) {
        destroy();
        return false;
    }
    depthPyramidPush_.counter =
        depthPyramidCounter_.span<GPUDepthPyramidCounter>(1).gpu.data;
    return true;
}

void Renderer::destroy() {
    depthPyramidCounter_.destroy();
    depthPyramidPass_.destroy();
    depthPyramid_.destroy();
    timings_.destroy();
    drawStats_.destroy();
    forward_.destroy();
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
                      const glm::mat4& viewProjection,
                      const glm::vec3& cameraPosition,
                      GpuPtr<GPUMaterial> materials) {
    timings_.beginFrame(recording);

    //Transition depth pyramid layout to DEPTH_PYRAMID_LAYOUT TODO: Shouldn't really happen here
    if (depthPyramidUndefined_) {
        depthPyramidCounter_.use(recording.commandBuffer(), TRANSFER_WRITE);
        fillBufferWithZero(recording.commandBuffer(), depthPyramidCounter_.region());
        depthPyramidUndefined_ = false;
    }

    //Prepare frame data. 
    const GpuPtr<GPUFrameData> frame = frameRecord_.write(
        recording.frameInFlight(),
        GPUFrameData{
            .camera       = GPUCameraData{viewProjection, glm::vec4(cameraPosition, 1.0f)},
            .drawCounters = drawStats_.beginFrame(recording, static_cast<uint32_t>(instances.size())),
            .debugShowMeshlets = debugFlags_.ShowMeshlets,
            .debugShowMeshletSpheres = debugFlags_.ShowMeshletSpheres,
            .debugDrawNormals = debugFlags_.DrawNormals,
            .useHiZOC = debugFlags_.UseHIZOC && depthPyramidBuiltLastFrame_,
            .hiZViewProj = depthPyramidViewProjection_,
        });
    //Push changed instances to gpu
    const PreparedMeshDraw meshDraw =
        meshDrawResources_.prepare(recording, instances, changed, frame, textures_->set(),
                                   depthPyramidSlot_, timings_);
    
    // Get the depth attachment for the current frame.
    const vk::RenderingAttachmentInfo depthAttachment =
        sharedTargets_.getDepthAttachment(recording);

    //Mesh shader samples depth pyramid, we need to transition it for the forward pass
    depthPyramid_.use(recording.commandBuffer(), DEPTH_PYRAMID_SAMPLED_READ);
    // Start forward pass
    forward_.render(recording, meshDraw, frame, materials,
                    textures_ ? textures_->set() : nullptr, depthAttachment, draw_,
                    timings_, depthPyramidSlot_);
    //Depth updated, we can update depth pyramid
    if (debugFlags_.UseHIZOC)
    {
        const vk::CommandBuffer cmd = recording.commandBuffer();
        //Set sampled depth image slot
        depthPyramidPush_.depthSlot = sharedTargets_.sampledDepth(recording).value;

        // Counter and depth pyramid are read and written by the build compute pass.
        use(cmd, {{depthPyramidCounter_, COMPUTE_WRITE},
                  {depthPyramid_, DEPTH_PYRAMID_COMPUTE_READ_WRITE}});
        
        const vk::Extent3D pyramidExtent = depthPyramid_.extent();
        depthPyramidPass_.recordGroups(cmd, depthPyramidPush_,
                                       std::max(1u, pyramidExtent.width / DEPTH_PYRAMID_TILE),
                                       std::max(1u, pyramidExtent.height / DEPTH_PYRAMID_TILE),
                                       textures_->set());

        timings_.mark(cmd, "build depth pyramid end");
        depthPyramidViewProjection_ = viewProjection;
    }
    depthPyramidBuiltLastFrame_ = debugFlags_.UseHIZOC;
    
    //Write stat for the frame end
    drawStats_.endFrame(recording);
}

bool Renderer::resize(vk::Extent2D extent) {
    if (!depthPyramid_.reallocate(depthPyramidDesc(extent)))
    {
        logError("Renderer: depth pyramid resize failed");
        return false;
    }
    depthPyramidUndefined_ = true;
    depthPyramidBuiltLastFrame_ = false;
    if (!textures_->replaceSampledBorrowed(depthPyramidSlot_,
                                           depthPyramid_.view(vk::ImageAspectFlagBits::eColor),
                                           DEPTH_PYRAMID_LAYOUT))
    {
        logError("Renderer: depth pyramid replacement failed");
        return false;
    }
    if (depthPyramid_.mipLevels() > DEPTH_PYRAMID_MAX_MIP_LEVELS) {
        logError("Renderer: depth pyramid has more mips than its storage slots");
        return false;
    }
    for (uint32_t level = 0; level < depthPyramidMipSlots_.size(); ++level) {
        if (!textures_->replaceStorage(depthPyramidMipSlots_[level],
                                       pyramidMipView(depthPyramid_, level)))
            return false;
    }
    depthPyramidPush_.mipCount = depthPyramid_.mipLevels();
    if (!sharedTargets_.resize(extent)) return false;
    return forward_.resize(extent);
}
