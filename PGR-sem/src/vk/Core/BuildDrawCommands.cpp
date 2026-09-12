module;
#include <vulkan/vulkan.hpp>

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

bool BuildDrawCommands::init(vk::Device device, ShaderLoader& shaderLoader,
                             const std::filesystem::path& shaderPath) {
    if (device_) {
        logError("BuildDrawCommands: init called twice");
        return false;
    }

    device_ = device;

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(BuildDrawCommandsPush);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (device_.createPipelineLayout(&layoutInfo, nullptr, &pipelineLayout_) != vk::Result::eSuccess) {
        logError("BuildDrawCommands: vkCreatePipelineLayout failed");
        destroy();
        return false;
    }

    vk::ShaderModule shader = shaderLoader.load(shaderPath);
    if (!shader) {
        logError("BuildDrawCommands: failed to load " + shaderPath.string());
        destroy();
        return false;
    }

    pipeline_ = createComputePipeline(device_, pipelineLayout_, shader);
    shaderLoader.destroy(shader);

    if (!pipeline_) {
        destroy();
        return false;
    }
    return true;
}

void BuildDrawCommands::record(vk::CommandBuffer commandBuffer,
                               const BuildDrawCommandsPush& push) const {
    if (!pipeline_ || push.instanceCount == 0) return;

    const uint32_t groupCount =
        (push.instanceCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_);
    commandBuffer.pushConstants(pipelineLayout_, vk::ShaderStageFlagBits::eCompute,
                       0, sizeof(push), &push);
    commandBuffer.dispatch(groupCount, 1, 1);
}

void BuildDrawCommands::destroy() {
    if (!device_) return;

    device_.destroyPipeline(pipeline_);
    device_.destroyPipelineLayout(pipelineLayout_);

    pipeline_       = nullptr;
    pipelineLayout_ = nullptr;
    device_         = nullptr;
}
