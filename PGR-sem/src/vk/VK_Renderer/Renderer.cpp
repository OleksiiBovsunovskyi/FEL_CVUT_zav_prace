module;

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <span>
#include <utility>

#include <glm/glm.hpp>

module Renderer;

namespace {

const std::filesystem::path SHADER_DIR{"Shaders"};

} // namespace

bool Renderer::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                    vk::Format colorFormat, vk::Extent2D extent) {
    if (!meshDrawResources_.init(
            ctx, shaderLoader, SHADER_DIR / "build_draw_commands.spv"))
        return false;

    if (!temporary_.init(ctx, shaderLoader, SHADER_DIR / "mesh.spv",
                         SHADER_DIR / "mesh_frag.spv", colorFormat, extent)) {
        meshDrawResources_.destroy();
        return false;
    }
    return true;
}

void Renderer::destroy() {
    temporary_.destroy();
    meshDrawResources_.destroy();
}

void Renderer::setDrawCallback(DrawFn cb) {
    temporary_.setDrawCallback(std::move(cb));
}

void Renderer::render(Frame::Recording& recording,
                      std::span<const GPUMeshInstance> instances,
                      const glm::mat4& viewProjection,
                      GpuPtr<GPUMaterial> materials) {
    const PreparedMeshDraw meshDraw =
        meshDrawResources_.prepare(recording, instances, viewProjection);
    temporary_.render(recording, meshDraw, viewProjection, materials);
}

bool Renderer::resize(vk::Extent2D extent) {
    return temporary_.resize(extent);
}
