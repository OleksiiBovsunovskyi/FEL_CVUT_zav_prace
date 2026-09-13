module;

#include <vulkan/vulkan.hpp>

#include <array>
#include <functional>
#include <optional>
#include <span>

#include <glm/glm.hpp>

export module Renderer;

import VulkanContext;
import Frame;
import GPUTypes;
import MeshDrawResources;
import ShadersLoader;
import ForwardRenderer;
import renderTarget;

/**
 * Owns per-frame depth targets shared by renderer passes.
 * In future probably will own other targets.
 * Stores Depth render target for all FRAMES_IN_FLIGHT, including past ones. TODO: store only 2.
 */
class SharedRenderTargets {
public:
    SharedRenderTargets() = default;
    ~SharedRenderTargets() { destroy(); }

    SharedRenderTargets(const SharedRenderTargets&)            = delete;
    SharedRenderTargets& operator=(const SharedRenderTargets&) = delete;

    [[nodiscard]] bool init(VulkanContext& ctx, vk::Extent2D extent);
    void destroy();
    [[nodiscard]] bool resize(vk::Extent2D extent);
    [[nodiscard]] vk::RenderingAttachmentInfo depthAttachment(
        Frame::Recording& recording);

private:
    std::array<std::optional<DepthRenderTarget>, FRAMES_IN_FLIGHT> depth_;
};

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

    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            vk::Format colorFormat, vk::Extent2D extent);

    void destroy();

    void setDrawCallback(DrawFn cb);

    /**
     * Prepares shared mesh-draw resources and records the current renderer.
     * @param recording active Frame recording interface.
     * @param instances visible mesh instances.
     * @param viewProjection world-to-clip matrix.
     * @param materials base address of the Materials mega-buffer.
     */
    void render(Frame::Recording& recording,
                std::span<const GPUMeshInstance> instances,
                const glm::mat4& viewProjection,
                GpuPtr<GPUMaterial> materials);

    [[nodiscard]] bool resize(vk::Extent2D extent);

private:
    MeshDrawResources   meshDrawResources_;
    SharedRenderTargets sharedTargets_;
    ForwardRenderer     forward_;
    DrawFn              draw_;
};
