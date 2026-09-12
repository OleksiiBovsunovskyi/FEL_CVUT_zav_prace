module;

#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

export module TemporaryRenderer;

import VulkanContext;
import VkUtil;
import FrameInFlightIndex;
import renderTarget;
import Frame;

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
     * Allocates per-frame depth targets.
     * @param ctx Vulkan context supplying allocator.
     * @param extent target size in texels.
     * @return false on allocation failure.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, vk::Extent2D extent);

    /// Destroys per-frame depth targets.
    void destroy();

    void setDrawCallback(DrawFn cb) { draw_ = std::move(cb); }

    /**
     * Records dynamic rendering pass and invokes draw callback.
     * @param recording active Frame recording interface.
     */
    void render(Frame::Recording& recording);

    /**
     * Resizes per-frame depth targets.
     * @param extent new size in texels.
     * @return false on reallocation failure.
     * @note The GPU must be done with every target before this is called.
     */
    [[nodiscard]] bool resize(vk::Extent2D extent);

private:
    std::array<std::optional<DepthRenderTarget>, FRAMES_IN_FLIGHT> depth_;
    DrawFn draw_;
};
