module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

module BuildDrawCommands;

import GPUTypes;
import Logger;
import Pipeline;

BuildDrawCommands::~BuildDrawCommands() {
    if (device_)
        logError("BuildDrawCommands: destroy() was not called before destruction");
}

bool BuildDrawCommands::init(VkDevice device, ShaderLoader& shaders,
                             const std::filesystem::path& shaderPath) {
    if (device_) {
        logError("BuildDrawCommands: init called twice");
        return false;
    }

    device_ = device;

    const VkPushConstantRange pushRange{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset     = 0,
        .size       = sizeof(BuildDrawCommandsPush),
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        logError("BuildDrawCommands: vkCreatePipelineLayout failed");
        destroy();
        return false;
    }

    VkShaderModule shader = shaders.load(shaderPath);
    if (!shader) {
        logError("BuildDrawCommands: failed to load " + shaderPath.string());
        destroy();
        return false;
    }

    pipeline_ = createComputePipeline(device_, pipelineLayout_, shader);
    shaders.destroy(shader);

    if (!pipeline_) {
        destroy();
        return false;
    }
    return true;
}

void BuildDrawCommands::record(VkCommandBuffer commandBuffer,
                               const BuildDrawCommandsPush& push) const {
    if (!pipeline_ || push.instanceCount == 0) return;

    const uint32_t groupCount =
        (push.instanceCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);
    vkCmdDispatch(commandBuffer, groupCount, 1, 1);
}

void BuildDrawCommands::destroy() {
    if (!device_) return;

    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);

    pipeline_       = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    device_         = VK_NULL_HANDLE;
}
