module;
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <filesystem>

module MeshDraw;

import vulkan;
import GPUTypes;
import Logger;
import Pipeline;
import VkUtil;

MeshDraw::~MeshDraw() {
    if (device_)
        logError("MeshDraw: destroy() was not called before destruction");
}

bool MeshDraw::init(vk::Device device, ShaderLoader& shaderLoader,
                    const std::filesystem::path& meshShaderPath,
                    const std::filesystem::path& fragmentShaderPath,
                    vk::Format colorFormat, vk::Format depthFormat,
                    PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount,
                    vk::DescriptorSetLayout textureLayout) {
    if (device_) {
        logError("MeshDraw: init called twice");
        return false;
    }
    if (!drawIndirectCount) {
        logError("MeshDraw: vkCmdDrawMeshTasksIndirectCountEXT is required");
        return false;
    }

    device_            = device;
    drawIndirectCount_ = drawIndirectCount;

    /* Buffer device addresses; the fragment stage reads the material record. */
    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(GPUMeshDrawPush);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    /* Set 0 is the bindless texture array. Null layout leaves the pipeline with no set */
    layoutInfo.setLayoutCount         = textureLayout ? 1u : 0u;
    layoutInfo.pSetLayouts            = &textureLayout;
    if (device_.createPipelineLayout(&layoutInfo, nullptr, &pipelineLayout_) != vk::Result::eSuccess) {
        logError("MeshDraw: vkCreatePipelineLayout failed");
        destroy();
        return false;
    }

    vk::ShaderModule meshShader = shaderLoader.load(meshShaderPath);
    vk::ShaderModule fragShader = shaderLoader.load(fragmentShaderPath);
    if (meshShader && fragShader) {
        pipeline_ = createMeshPipeline(device_, pipelineLayout_, meshShader, fragShader,
                                       colorFormat, depthFormat);
    } else {
        logError("MeshDraw: failed to load " + meshShaderPath.string() + " / " +
                 fragmentShaderPath.string());
    }
    shaderLoader.destroy(meshShader);
    shaderLoader.destroy(fragShader);

    if (!pipeline_) {
        destroy();
        return false;
    }
    return true;
}

void MeshDraw::record(vk::CommandBuffer commandBuffer, vk::Extent2D extent,
                      const GPUMeshDrawPush& push,
                      const BufferRegion& commands, const BufferRegion& count,
                      uint32_t maxDrawCount, vk::DescriptorSet textureSet) const {
    if (!pipeline_ || !commands || !count || maxDrawCount == 0) return;

    const auto [viewport, scissor] = viewportAndScissor(extent);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
    if (textureSet)
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout_, 0, 1, &textureSet, 0, nullptr);
    commandBuffer.setViewport(0, 1, &viewport);
    commandBuffer.setScissor(0, 1, &scissor);
    commandBuffer.pushConstants(pipelineLayout_,
                       vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment,
                       0, sizeof(push), &push);

    drawIndirectCount_(static_cast<VkCommandBuffer>(commandBuffer),
                       static_cast<VkBuffer>(commands.buffer), commands.offset,
                       static_cast<VkBuffer>(count.buffer), count.offset,
                       maxDrawCount, sizeof(GPUMeshTaskCommand));
}

void MeshDraw::destroy() {
    if (!device_) return;

    device_.destroyPipeline(pipeline_);
    device_.destroyPipelineLayout(pipelineLayout_);

    pipeline_          = nullptr;
    pipelineLayout_    = nullptr;
    device_            = nullptr;
    drawIndirectCount_ = nullptr;
}
