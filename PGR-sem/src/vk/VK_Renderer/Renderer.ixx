module;

#include <vulkan/vulkan.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

export module Renderer;

import VulkanContext;
import VkUtil;
import FrameInFlightIndex;
import renderTarget;
import Frame;

/**
 * Owns per-frame depth render targets
 * Targets allocated with Renderer::init().
 * Dynamic rendering pass opened with Renderer::render().
 * Targets destroyed on destruction or Renderer::destroy().
 */
export class Renderer {
public:
    /**
     * Draw callback invoked inside Renderer::render().
     * @param cmd active command buffer.
     * @param extent render area size in texels.
     */
    using DrawFn = std::function<void(vk::CommandBuffer, vk::Extent2D)>;

    Renderer() = default;
    ~Renderer() { destroy(); }

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    /**
     * Allocates per-frame depth targets.
     * @param ctx Vulkan context supplying allocator.
     * @param extent target size in texels.
     * @return false on allocation failure.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, vk::Extent2D extent);

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
