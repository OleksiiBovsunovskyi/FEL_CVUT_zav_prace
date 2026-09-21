module;

#include <cstdint>
#include <filesystem>
#include <string>

export module ComputePass;

import vulkan;
import Logger;
import Pipeline;
import ShadersLoader;

/**
 * Owns one compute pipeline whose only argument is a Push record.
 */
export template <typename Push>
class ComputePass {
public:
    ComputePass() = default;
    ~ComputePass() {
        if (device_)
            logError("ComputePass: destroy() was not called before destruction");
    }

    ComputePass(const ComputePass&)            = delete;
    ComputePass& operator=(const ComputePass&) = delete;

    /**
     * @param shaderPath SPIR-V module whose entry point is "main".
     * @return false when called twice, or when the module or the pipeline failed.
     */
    [[nodiscard]] bool init(vk::Device device, ShaderLoader& shaderLoader,
                            const std::filesystem::path& shaderPath);

    /**
     * Records one invocation per item, in groups of ComputePass::WORKGROUP_SIZE.
     * @param push the shader's arguments.
     * @param itemCount invocations wanted;
     * @note Records nothing when itemCount is 0.
     */
    void record(vk::CommandBuffer commandBuffer, const Push& push,
                uint32_t itemCount) const;

    void destroy();

    /// Invocations per workgroup; 
    static constexpr uint32_t WORKGROUP_SIZE = 64;

private:
    vk::Device         device_         = nullptr;
    vk::PipelineLayout pipelineLayout_ = nullptr;
    vk::Pipeline       pipeline_       = nullptr;
};

/* ------------------------------------------------------------------------ */

template <typename Push>
bool ComputePass<Push>::init(vk::Device device, ShaderLoader& shaderLoader,
                             const std::filesystem::path& shaderPath) {
    if (device_) {
        logError("ComputePass: init called twice for " + shaderPath.string());
        return false;
    }

    device_ = device;

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(Push);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (device_.createPipelineLayout(&layoutInfo, nullptr, &pipelineLayout_) !=
        vk::Result::eSuccess) {
        logError("ComputePass: vkCreatePipelineLayout failed for " +
                 shaderPath.string());
        destroy();
        return false;
    }

    vk::ShaderModule shader = shaderLoader.load(shaderPath);
    if (!shader) {
        logError("ComputePass: failed to load " + shaderPath.string());
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

template <typename Push>
void ComputePass<Push>::record(vk::CommandBuffer commandBuffer, const Push& push,
                               uint32_t itemCount) const {
    if (!pipeline_ || itemCount == 0) return;

    const uint32_t groupCount = (itemCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_);
    commandBuffer.pushConstants(pipelineLayout_, vk::ShaderStageFlagBits::eCompute,
                                0, sizeof(push), &push);
    commandBuffer.dispatch(groupCount, 1, 1);
}

template <typename Push>
void ComputePass<Push>::destroy() {
    if (!device_) return;

    device_.destroyPipeline(pipeline_);
    device_.destroyPipelineLayout(pipelineLayout_);

    pipeline_       = nullptr;
    pipelineLayout_ = nullptr;
    device_         = nullptr;
}
