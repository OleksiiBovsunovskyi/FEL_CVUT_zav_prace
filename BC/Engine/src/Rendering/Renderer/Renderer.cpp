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
import Resource;
import RendererConfig;

namespace {

/// Pyramid mip 0 texels one build_depth_pyramid workgroup covers per side.
/// Force this agreeing between shader and c++ side
constexpr uint32_t DEPTH_PYRAMID_TILE = 64;

} // namespace

bool Renderer::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                    vk::Format colorFormat, vk::Extent2D extent,
                    TextureManager& textures) {
    textures_ = &textures;
    if (!meshDrawResources_.init(ctx, shaderLoader,
                                 "Core/scatter_instances.slang",
                                 "Core/build_draw_commands.slang", textures.layout()))
        return false;

    if (!frameRecord_.init(ctx, "frame")) {
        meshDrawResources_.destroy();
        return false;
    }

    if (!sharedResources_.init(ctx, extent, textures)) {
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }
    //TODO: Add mesh shader override. 
    if (!forward_.init(ctx, shaderLoader, "Core/Renderer/Forward/forward_mesh.slang",
                       "Core/Renderer/Forward/forward_frag.slang", colorFormat, extent,
                       textures.layout())) {
        sharedResources_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }
    if (!deferred_.init(ctx, shaderLoader,
        "Core/Renderer/Deferred/deferred_mesh.slang",
        "Core/Renderer/Deferred/DirectlySampleVBuffer/deferred_vBuffer_direct_frag.slang",
        "Core/Renderer/Deferred/visibility_buffer_mesh.slang",
        "Core/Renderer/Deferred/visibility_buffer_frag.slang",
        colorFormat,
        extent,
        textures.layout())) {

        forward_.destroy();
        sharedResources_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }
    if (!drawStats_.init(ctx)) {
        deferred_.destroy();
        forward_.destroy();
        sharedResources_.destroy();
        frameRecord_.destroy();
        meshDrawResources_.destroy();
        return false;
    }

    if (!timings_.init(ctx)) logError("Renderer: GPU pass timings unavailable");
    

    depthPyramidCounterUnset_ = true;
    depthPyramidPush_.pyramidMip0Slot = sharedResources_.getDepthPyramid().storageMips().first.value;
    depthPyramidPush_.mipCount        = sharedResources_.getDepthPyramid().storageMips().count;
    //Prepare depth pyramid build pass
    if (!depthPyramidPass_.init(ctx.device(), shaderLoader, "Core/build_depth_pyramid.slang",
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
    timings_.destroy();
    drawStats_.destroy();
    deferred_.destroy();
    forward_.destroy();
    sharedResources_.destroy();
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

    if (depthPyramidCounterUnset_) {
        depthPyramidCounter_.use(recording.commandBuffer(), TRANSFER_WRITE);
        fillBufferWithZero(recording.commandBuffer(), depthPyramidCounter_.region());
        depthPyramidCounterUnset_ = false;
    }
    RegisteredImage& depthPyramid = sharedResources_.getDepthPyramid();

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
                                   depthPyramid.sampledSlot(), timings_);
    
    // Get the depth attachment for the current frame.
    const vk::RenderingAttachmentInfo depthAttachment =
        sharedResources_.getDepthAttachment(recording);

    //Mesh shader samples depth pyramid, we need to transition it for the forward pass
    depthPyramid.image().use(recording.commandBuffer(), DEPTH_PYRAMID_SAMPLED_READ);
    // Start forward pass
    deferred_.render(recording, meshDraw, frame, materials, textures_?  textures_->set():nullptr,
                     sharedResources_, draw_, timings_, depthPyramid.sampledSlot());
    //! forward_.render(recording, meshDraw, frame, materials,
    //!                 textures_ ? textures_->set() : nullptr, depthAttachment, draw_,
    //!                 timings_, depthPyramid.sampledSlot());
    //! //Depth updated, we can update depth pyramid
    if (debugFlags_.UseHIZOC)
    {
        const vk::CommandBuffer cmd = recording.commandBuffer();
        //Set sampled depth image slot
        depthPyramidPush_.depthSlot = sharedResources_.getSampledDepthSlot(recording).value;

        // Counter and depth pyramid are read and written by the build compute pass.
        use(cmd, {{depthPyramidCounter_, COMPUTE_WRITE},
                  {depthPyramid.image(), DEPTH_PYRAMID_COMPUTE_READ_WRITE}});

        const vk::Extent3D pyramidExtent = depthPyramid.image().desc().extent;
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
    if (!sharedResources_.resize(extent)) return false;
    depthPyramidCounterUnset_   = true;
    depthPyramidBuiltLastFrame_ = false;
    depthPyramidPush_.pyramidMip0Slot = sharedResources_.getDepthPyramid().storageMips().first.value;
    depthPyramidPush_.mipCount        = sharedResources_.getDepthPyramid().storageMips().count;
    return forward_.resize(extent);
}
