module;

#include <vulkan/vulkan.hpp>

export module Renderer;

import VulkanContext;
import Frame;
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

    [[nodiscard]] bool init(VulkanContext& ctx, vk::Extent2D extent);

    void destroy();

    void setDrawCallback(DrawFn cb);

    void render(Frame::Recording& recording);

    [[nodiscard]] bool resize(vk::Extent2D extent);

private:
    TemporaryRenderer temporary_;
};
