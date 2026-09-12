module;

#include <vulkan/vulkan.hpp>

#include <span>

#include <glm/glm.hpp>

export module Renderer;

import VulkanContext;
import Frame;
import GPUTypes;
import MeshDrawResources;
import ShadersLoader;
import TemporaryRenderer;

/**
 * Owns and calls all the renderers in use, passing them frame and buffers they need.
 * Owns per frame buffers needed for rendering. (Currently only calls TemporaryRenderer).
 */
export class Renderer {
public:
    using DrawFn = TemporaryRenderer::DrawFn;

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
    MeshDrawResources meshDrawResources_;
    TemporaryRenderer  temporary_;
};
