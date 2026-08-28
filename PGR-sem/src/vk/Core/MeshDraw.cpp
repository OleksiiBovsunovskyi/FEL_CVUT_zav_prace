module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

module MeshDraw;

import GPUTypes;
import Logger;
import Pipeline;

MeshDraw::~MeshDraw() {
    if (device_)
        logError("MeshDraw: destroy() was not called before destruction");
}

bool MeshDraw::init(VkDevice device, ShaderLoader& shaders,
                    const std::filesystem::path& meshShaderPath,
                    const std::filesystem::path& fragmentShaderPath,
                    VkFormat colorFormat, VkFormat depthFormat,
                    PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount) {
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

    /* Every buffer is reached through its device address, so no descriptor sets. */
    const VkPushConstantRange pushRange{
        .stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT,
        .offset     = 0,
        .size       = sizeof(MeshDrawPush),
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        logError("MeshDraw: vkCreatePipelineLayout failed");
        destroy();
        return false;
    }

    VkShaderModule meshShader = shaders.load(meshShaderPath);
    VkShaderModule fragShader = shaders.load(fragmentShaderPath);
    if (meshShader && fragShader) {
        pipeline_ = createMeshPipeline(device_, pipelineLayout_, meshShader, fragShader,
                                       colorFormat, depthFormat);
    } else {
        logError("MeshDraw: failed to load " + meshShaderPath.string() + " / " +
                 fragmentShaderPath.string());
    }
    shaders.destroy(meshShader);
    shaders.destroy(fragShader);

    if (!pipeline_) {
        destroy();
        return false;
    }
    return true;
}

void MeshDraw::record(VkCommandBuffer commandBuffer, VkExtent2D extent,
                      const MeshDrawPush& push,
                      VkBuffer commands, VkDeviceSize commandsOffset,
                      VkBuffer count, VkDeviceSize countOffset,
                      uint32_t maxDrawCount) const {
    if (!pipeline_ || !commands || !count || maxDrawCount == 0) return;

    const VkViewport viewport{
        .x = 0.0f, .y = 0.0f,
        .width  = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    const VkRect2D scissor{ {0, 0}, extent };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_MESH_BIT_EXT,
                       0, sizeof(push), &push);

    drawIndirectCount_(commandBuffer, commands, commandsOffset, count, countOffset,
                       maxDrawCount, sizeof(GPUMeshTaskCommand));
}

void MeshDraw::destroy() {
    if (!device_) return;

    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);

    pipeline_          = VK_NULL_HANDLE;
    pipelineLayout_    = VK_NULL_HANDLE;
    device_            = VK_NULL_HANDLE;
    drawIndirectCount_ = nullptr;
}
