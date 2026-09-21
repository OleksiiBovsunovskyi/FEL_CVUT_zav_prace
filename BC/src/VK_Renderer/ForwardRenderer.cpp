module;

#include <array>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>

module ForwardRenderer;

import vulkan;
import VkUtil;

bool ForwardRenderer::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                           const std::filesystem::path& meshShaderPath,
                           const std::filesystem::path& fragmentShaderPath,
                           vk::Format colorFormat, vk::Extent2D,
                           vk::DescriptorSetLayout textureLayout) {
    if (!meshDraw_.init(ctx.device(), shaderLoader, meshShaderPath, fragmentShaderPath,
                        colorFormat, DEPTH_FORMAT, ctx.cmdDrawMeshTasksIndirectCount(),
                        textureLayout)) {
        destroy();
        return false;
    }
    return true;
}

void ForwardRenderer::destroy() {
    meshDraw_.destroy();
}

bool ForwardRenderer::resize(vk::Extent2D) {
    return true;
}

void ForwardRenderer::render(
    Frame::Recording& recording, const PreparedMeshDraw& preparedMeshDraw,
    const glm::mat4& viewProjection, const glm::vec3& cameraPosition,
    GpuPtr<GPUMaterial> materials, vk::DescriptorSet textureSet,
    const vk::RenderingAttachmentInfo& depthAttachment,
    const std::function<void(vk::CommandBuffer, vk::Extent2D)>& drawCallback) {
    const vk::CommandBuffer cmd = recording.commandBuffer();

    const vk::RenderingAttachmentInfo colorAttachment =
        recording.colorAttachment(vk::ClearColorValue{std::array<float, 4>{0.2f, 0.1f, 0.3f, 1.0f}});
    vk::RenderingInfo rendering{};
    rendering.renderArea = vk::Rect2D{{0, 0}, recording.extent()};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colorAttachment;
    rendering.pDepthAttachment = &depthAttachment;

    cmd.beginRendering(rendering);
    if (preparedMeshDraw) {
        GPUMeshDrawPush push{};
        push.viewProj = viewProjection;
        push.drawData = preparedMeshDraw.drawData;
        push.instances = preparedMeshDraw.instances;
        push.materials = materials;
        push.cameraPosition = glm::vec4(cameraPosition, 1.0f);
        meshDraw_.record(cmd, recording.extent(), push,
                         preparedMeshDraw.indirectCommands,
                         preparedMeshDraw.indirectCount,
                         preparedMeshDraw.instanceCount, textureSet);
    }
    if (drawCallback) drawCallback(cmd, recording.extent());
    cmd.endRendering();
}
