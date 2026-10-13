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
import ComputePass;

namespace {

const std::filesystem::path SHADER_DIR{"Shaders"};

/// Layout shared depth targets are sampled in after their pass.
constexpr vk::ImageLayout SAMPLED_DEPTH_LAYOUT = vk::ImageLayout::eShaderReadOnlyOptimal;

/// Layout the depth pyramid is built and sampled in.
constexpr vk::ImageLayout DEPTH_PYRAMID_LAYOUT = vk::ImageLayout::eGeneral;

/// Pyramid mip 0 texels one build_depth_pyramid workgroup covers per side.
/// Force this agreeing between shader and c++ side
constexpr uint32_t DEPTH_PYRAMID_TILE = 64;

constexpr BufferAccess DEPTH_PYRAMID_COUNTER_CLEAR{vk::PipelineStageFlagBits2::eAllTransfer,
                                                   vk::AccessFlagBits2::eTransferWrite};
constexpr BufferAccess DEPTH_PYRAMID_COUNTER_BUILD{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};

/**
 * @param pyramid pyramid the view is taken from.
 * @param level mip level
 * @return a view of mip `level`, or of the last mip past the chain.
 */
ImageRef pyramidMipView(DepthPyramid& pyramid, uint32_t level) {
    return pyramid.mipView(std::min(level, pyramid.mipLevels() - 1));
}

} // namespace

bool SharedRenderTargets::init(VulkanContext& ctx, vk::Extent2D extent,
                               TextureManager& textures) {
    textures_ = &textures;
    for (size_t i = 0; i < depth_.size(); ++i) {
        auto& depth = depth_[i];
        depth.emplace(ctx.allocator(), extent);
        if (!depth->create()) {
            logError("SharedRenderTargets::init: depth target allocation failed");
            destroy();
            return false;
        }
        depthSlots_[i] = textures.addSampledBorrowed(depth->view(), SAMPLED_DEPTH_LAYOUT);
        if (!depthSlots_[i]) {
            logError("SharedRenderTargets::init: depth target registration failed");
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
    for (size_t i = 0; i < depth_.size(); ++i) {
        auto& depth = depth_[i];
        if (!depth || !depth->resize(extent)) {
            logError("SharedRenderTargets::resize: depth target reallocation failed");
            return false;
        }
        if (!textures_->replaceSampledBorrowed(depthSlots_[i], depth->view(),
                                               SAMPLED_DEPTH_LAYOUT)) {
            logError("SharedRenderTargets::resize: depth target re-registration failed");
            return false;
        }
    }
    return true;
}

TextureSlot SharedRenderTargets::sampledDepth(Frame::Recording& recording) {
    DepthRenderTarget& depth = *recording.select(depth_);
    transitionImage(recording.commandBuffer(), depth.image().handle(),
                    vk::ImageLayout::eDepthAttachmentOptimal, SAMPLED_DEPTH_LAYOUT,
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderSampledRead,
                    vk::ImageAspectFlagBits::eDepth);
    return recording.select(depthSlots_);
}

vk::RenderingAttachmentInfo SharedRenderTargets::getDepthAttachment(
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
    depthPyramid_.emplace(ctx.allocator(), extent);
    if (!depthPyramid_->create()) {
        logError("Renderer: depth pyramid allocation failed");
        destroy();
        return false;
    }
    depthPyramidUndefined_ = true;
    //Register with texture manager
    depthPyramidSlot_ = textures_->addSampledBorrowed(depthPyramid_->view(), DEPTH_PYRAMID_LAYOUT);
    if (!depthPyramidSlot_) {
        destroy();
        return false;
    }
    
    if (depthPyramid_->mipLevels() > DepthPyramid::MAX_MIP_LEVELS) {
        logError("Renderer: depth pyramid has more mips than its storage slots");
        destroy();
        return false;
    }
    // Create storage slots for each of depth pyramid mips
    for (uint32_t level = 0; level < depthPyramidMipSlots_.size(); ++level) {
        depthPyramidMipSlots_[level] =
            textures_->addStorage(pyramidMipView(*depthPyramid_, level));
        if (depthPyramidMipSlots_[level].value != depthPyramidMipSlots_[0].value + level) {
            logError("Renderer: depth pyramid storage slots are not consecutive");
            destroy();
            return false;
        }
    }
    depthPyramidPush_.pyramidMip0Slot = depthPyramidMipSlots_[0].value;
    depthPyramidPush_.mipCount        = depthPyramid_->mipLevels();
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
    depthPyramid_.reset();
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

    const bool depthPyramidBuilt = !depthPyramidUndefined_;
    //Transition depth pyramid layout to DEPTH_PYRAMID_LAYOUT TODO: Shouldn't really happen here
    if (depthPyramidUndefined_) {
        transitionImage(recording.commandBuffer(), depthPyramid_->image().handle(),
                        vk::ImageLayout::eUndefined, DEPTH_PYRAMID_LAYOUT,
                        vk::PipelineStageFlagBits2::eNone, {},
                        vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderStorageWrite |
                            vk::AccessFlagBits2::eShaderSampledRead,
                        vk::ImageAspectFlagBits::eColor, 0, depthPyramid_->mipLevels());
        depthPyramidCounter_.resetAccess();
        (void)depthPyramidCounter_.use(DEPTH_PYRAMID_COUNTER_CLEAR);
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
            .useHiZOC = depthPyramidBuilt,
            .hiZViewProj = depthPyramidViewProjection_,
        });
    //Push changed instances to gpu
    const PreparedMeshDraw meshDraw =
        meshDrawResources_.prepare(recording, instances, changed, frame, textures_->set(),
                                   depthPyramidSlot_, timings_);
    
    // Get the depth attachment for the current frame.
    const vk::RenderingAttachmentInfo depthAttachment =
        sharedTargets_.getDepthAttachment(recording);
    // Start forward pass
    forward_.render(recording, meshDraw, frame, materials,
                    textures_ ? textures_->set() : nullptr, depthAttachment, draw_,
                    timings_, depthPyramidSlot_);
    //Depth updated, we can update depth pyramid
    if (debugFlags_.UseHIZOC)
    {
        const vk::CommandBuffer cmd = recording.commandBuffer();
        depthPyramidPush_.depthSlot = sharedTargets_.sampledDepth(recording).value;
        if (const auto barrier = depthPyramidCounter_.use(DEPTH_PYRAMID_COUNTER_BUILD))
            recordBarriers(cmd, std::span(&*barrier, 1));
        // Earlier sampled reads of the pyramid finish before the build overwrites it.
        transitionImage(cmd, depthPyramid_->image().handle(),
                        DEPTH_PYRAMID_LAYOUT, DEPTH_PYRAMID_LAYOUT,
                        vk::PipelineStageFlagBits2::eComputeShader |
                            vk::PipelineStageFlagBits2::eMeshShaderEXT |
                            vk::PipelineStageFlagBits2::eFragmentShader,
                        vk::AccessFlagBits2::eShaderSampledRead,
                        vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderStorageRead |
                            vk::AccessFlagBits2::eShaderStorageWrite,
                        vk::ImageAspectFlagBits::eColor, 0, depthPyramid_->mipLevels());
        const vk::Extent3D pyramidExtent = depthPyramid_->extent();
        depthPyramidPass_.recordGroups(cmd, depthPyramidPush_,
                                       std::max(1u, pyramidExtent.width / DEPTH_PYRAMID_TILE),
                                       std::max(1u, pyramidExtent.height / DEPTH_PYRAMID_TILE),
                                       textures_->set());
        // Build writes are visible to the next frame's sampled reads.
        transitionImage(cmd, depthPyramid_->image().handle(),
                        DEPTH_PYRAMID_LAYOUT, DEPTH_PYRAMID_LAYOUT,
                        vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderStorageWrite,
                        vk::PipelineStageFlagBits2::eComputeShader |
                            vk::PipelineStageFlagBits2::eMeshShaderEXT |
                            vk::PipelineStageFlagBits2::eFragmentShader,
                        vk::AccessFlagBits2::eShaderSampledRead,
                        vk::ImageAspectFlagBits::eColor, 0, depthPyramid_->mipLevels());
        timings_.mark(cmd, "build depth pyramid end");
    }
    depthPyramidViewProjection_ = viewProjection;
    
    //Write stat for the frame end
    drawStats_.endFrame(recording);
}

bool Renderer::resize(vk::Extent2D extent) {
    if (!depthPyramid_->resize(extent))
    {
        logError("Renderer: depth pyramid resize failed");
        return false;
    }
    depthPyramidUndefined_ = true;
    if (!textures_->replaceSampledBorrowed(depthPyramidSlot_, depthPyramid_->view(), DEPTH_PYRAMID_LAYOUT))
    {
        logError("Renderer: depth pyramid replacement failed");
        return false;
    }
    if (depthPyramid_->mipLevels() > DepthPyramid::MAX_MIP_LEVELS) {
        logError("Renderer: depth pyramid has more mips than its storage slots");
        return false;
    }
    for (uint32_t level = 0; level < depthPyramidMipSlots_.size(); ++level) {
        if (!textures_->replaceStorage(depthPyramidMipSlots_[level],
                                       pyramidMipView(*depthPyramid_, level)))
            return false;
    }
    depthPyramidPush_.mipCount = depthPyramid_->mipLevels();
    if (!sharedTargets_.resize(extent)) return false;
    return forward_.resize(extent);
}
