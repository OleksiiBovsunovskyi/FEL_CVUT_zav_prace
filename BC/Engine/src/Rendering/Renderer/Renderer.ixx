module;

#include <array>
#include <cstdint>
#include <functional>
#include <span>

#include <glm/glm.hpp>

export module Renderer;

import vulkan;
import DebugFlags;
import VulkanContext;
import Frame;
import GPUTypes;
import MeshDrawResources;
import ShadersLoader;
import VK_Buffers;
import ForwardRenderer;
export import GpuPassTimings;
export import GpuDrawStats;
import Image;
import TextureManager;
import RegisteredImage;
import ComputePass;
import DeferredRenderer;
import SharedResources;

/**
 * Owns shared mesh-draw resources, shared depth targets, renderers, and the draw callback.
 */
export class Renderer {
public:
    using DrawFn = std::function<void(vk::CommandBuffer, vk::Extent2D)>;

    Renderer() = default;
    ~Renderer() { destroy(); }

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    /**
     * @param textures already initialized; its set layout goes into the mesh
     *        pipeline layout and must outlive this Renderer.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            vk::Format colorFormat, vk::Extent2D extent,
                            TextureManager& textures);

    void destroy();

    void setDrawCallback(DrawFn cb);

    /**
     * Prepares shared mesh-draw resources and records the current renderer.
     * @param recording active Frame recording interface.
     * @param instances every registered mesh instance, in DrawList order.
     * @param changed indices of `instances` written since the previous call.
     * @param viewProjection world-to-clip matrix.
     * @param cameraPosition world-space eye position.
     * @param materials base address of the Materials mega-buffer.
     * @note Must be called once per recording even with no instances, or
     *       `changed` never reaches the frame-in-flight slots that owe it.
     */
    void render(Frame::Recording& recording,
                std::span<const Instance> instances,
                std::span<const uint32_t> changed,
                const glm::mat4& viewProjection,
                const glm::vec3& cameraPosition,
                GpuPtr<GPUMaterial> materials);

    [[nodiscard]] bool resize(vk::Extent2D extent);

    /// @return counters of the most recently completed recording.
    [[nodiscard]] const GpuDrawStats& drawStats() const { return drawStats_; }

    /// @return GPU time per pass of the most recently completed recording.
    [[nodiscard]] std::span<const GpuPassTimings::Timing> timings() const {
        return timings_.timings();
    }
    
    DebugFlags getDebugFlags() const { return debugFlags_; }
    void setDebugFlags(DebugFlags flags) { debugFlags_ = flags; }
private:
    ///Renderers
    ForwardRenderer       forward_;
    DeferredRenderer      deferred_;

    ///Stats
    GpuPassTimings        timings_;
    GpuDrawStats          drawStats_;

    ///Passes
    ComputePass<GPUDepthPyramidPush> depthPyramidPass_;

    ///Resources
    TextureManager* textures_ = nullptr;
    SharedResources sharedResources_;

    PerFrameRecord<GPUFrameData> frameRecord_;
    MeshDrawResources     meshDrawResources_;

    /// True until depthPyramidCounter_ is zeroed after init or resize.
    bool                  depthPyramidCounterUnset_ = true;
    /// World-to-clip matrix of the depth the pyramid was last built from.
    glm::mat4             depthPyramidViewProjection_{1.0f};
    AllocatedBuffer<DeviceOnlyBuffer> depthPyramidCounter_;
    /// Pyramid holds the previous frame's depth, built with depthPyramidViewProjection_.
    bool                  depthPyramidBuiltLastFrame_ = false;
    /// Push of depthPyramidPass_; its counter points into depthPyramidCounter_.
    GPUDepthPyramidPush   depthPyramidPush_{};

    DrawFn                draw_;
    DebugFlags            debugFlags_;

};
