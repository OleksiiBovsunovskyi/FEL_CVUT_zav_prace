module;

#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>

#include <glm/glm.hpp>

export module TemporaryRenderer;

import VulkanContext;
import VkUtil;
import FrameInFlightIndex;
import Frame;
import GPUTypes;
import MeshDraw;
import MeshDrawResources;
import renderTarget;
import ShadersLoader;

/**
 * Owns the current depth targets and records the dynamic-rendering pass.
 * Targets are allocated with TemporaryRenderer::init().
 * Draw callback is invoked inside TemporaryRenderer::render().
 * !TODO: replace with proper renderer.
 */
export class TemporaryRenderer {
public:
    /**
     * Draw callback invoked inside TemporaryRenderer::render().
     * @param cmd active command buffer.
     * @param extent render area size in texels.
     */
    using DrawFn = std::function<void(vk::CommandBuffer, vk::Extent2D)>;

    TemporaryRenderer() = default;
    ~TemporaryRenderer() { destroy(); }

    TemporaryRenderer(const TemporaryRenderer&)            = delete;
    TemporaryRenderer& operator=(const TemporaryRenderer&) = delete;

    /**
     * Allocates per-frame depth targets and creates the mesh-draw pipeline.
     * @param ctx Vulkan context supplying allocator and indirect-draw function.
     * @param shaderLoader loads the mesh and fragment shader modules.
     * @param meshShaderPath mesh shader SPIR-V file.
     * @param fragmentShaderPath fragment shader SPIR-V file.
     * @param colorFormat swapchain format.
     * @param extent target size in texels.
     * @return false on allocation or pipeline creation failure.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            const std::filesystem::path& meshShaderPath,
                            const std::filesystem::path& fragmentShaderPath,
                            vk::Format colorFormat, vk::Extent2D extent);

    /// Destroys per-frame depth targets.
    void destroy();

    void setDrawCallback(DrawFn cb) { draw_ = std::move(cb); }

    /**
     * Records dynamic rendering, the prepared mesh draw, and the draw callback.
     * @param recording active Frame recording interface.
     * @param meshDraw prepared mesh-draw resources for this recording.
     * @param viewProjection world-to-clip matrix for the mesh draw.
     * @param materials base address of the Materials mega-buffer.
     */
    void render(Frame::Recording& recording, const PreparedMeshDraw& meshDraw,
                const glm::mat4& viewProjection, GpuPtr<GPUMaterial> materials);

    /**
     * Resizes per-frame depth targets.
     * @param extent new size in texels.
     * @return false on reallocation failure.
     * @note The GPU must be done with every target before this is called.
     */
    [[nodiscard]] bool resize(vk::Extent2D extent);

private:
    MeshDraw meshDraw_;
    std::array<std::optional<DepthRenderTarget>, FRAMES_IN_FLIGHT> depth_;
    DrawFn draw_;
};
