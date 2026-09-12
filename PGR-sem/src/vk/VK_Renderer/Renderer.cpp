module;

#include <vulkan/vulkan.hpp>

#include <utility>

module Renderer;

bool Renderer::init(VulkanContext& ctx, vk::Extent2D extent) {
    return temporary_.init(ctx, extent);
}

void Renderer::destroy() {
    temporary_.destroy();
}

void Renderer::setDrawCallback(DrawFn cb) {
    temporary_.setDrawCallback(std::move(cb));
}

void Renderer::render(Frame::Recording& recording) {
    temporary_.render(recording);
}

bool Renderer::resize(vk::Extent2D extent) {
    return temporary_.resize(extent);
}
