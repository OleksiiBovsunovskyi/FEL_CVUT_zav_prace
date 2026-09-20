module;

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

#include <glm/glm.hpp>

export module Renderer;

import vulkan;
import VulkanContext;
import Frame;
import GPUTypes;
import MeshDrawResources;
import ShadersLoader;
import ForwardRenderer;
import renderTarget;
import TextureManager;

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

    /**
     * @param textures already initialized; its set layout goes into the mesh
     *        pipeline layout and must outlive this Renderer.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            vk::Format colorFormat, vk::Extent2D extent,
                            const TextureManager& textures);

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
                std::span<const GPUMeshInstance> instances,
                std::span<const uint32_t> changed,
                const glm::mat4& viewProjection,
                const glm::vec3& cameraPosition,
                GpuPtr<GPUMaterial> materials);

    [[nodiscard]] bool resize(vk::Extent2D extent);

private:
    MeshDrawResources     meshDrawResources_;
    SharedRenderTargets   sharedTargets_;
    ForwardRenderer       forward_;
    DrawFn                draw_;
    const TextureManager* textures_ = nullptr;
};
