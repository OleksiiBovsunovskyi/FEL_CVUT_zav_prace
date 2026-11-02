module;
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <filesystem>

export module DrawPass;

import vulkan;
import GPUTypes;
import Logger;
import Pipeline;
import ShadersLoader;
import VkUtil;
import VK_Buffers;

/**
 * Owns one mesh-shader graphics pipeline whose arguments are a Push record
 * Issues the indirect draw whose commands build_draw_commands.comp produced.
 * Push is visible to the mesh and fragment stages.
 */
export template <typename Push>
class DrawPass {
public:
    DrawPass() = default;
    ~DrawPass() {
        if (device_)
            logError("DrawPass: destroy() was not called before destruction");
    }

    DrawPass(const DrawPass&) = delete;
    DrawPass& operator=(const DrawPass&) = delete;

    /**
     * @param drawIndirectCount vkCmdDrawMeshTasksIndirectCountEXT, from
     *        VulkanContext.
     * @param colorFormat format of the single colour attachment the pass renders into.
     * @param textureLayout layout of set 0, or null for a pipeline with no sets.
     */
    [[nodiscard]] bool init(vk::Device device, ShaderLoader& shaderLoader,
                            const std::filesystem::path& meshShaderPath,
                            const std::filesystem::path& fragmentShaderPath,
                            vk::Format colorFormat, vk::Format depthFormat,
                            PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount,
                            vk::DescriptorSetLayout textureLayout, bool disableDepthTest = false, bool disableDepthWrite = false);

    /**
     * Records one indirect draw.
     * @param commands the GPUMeshTaskCommand array built by the compute pass.
     * @param count the draw count it wrote; both offsets must be 4-byte aligned.
     * @param maxDrawCount upper bound the count is clamped against - the object
     *        count the command buffer was sized for.
     * @note Must be inside a render pass whose attachment formats match those given to init().
     */
    void record(vk::CommandBuffer commandBuffer, vk::Extent2D extent,
                const Push& push,
                const BufferRegion& commands, const BufferRegion& count,
                uint32_t maxDrawCount, vk::DescriptorSet textureSet) const;

    /**
     * Record version for the single direct draw command
     */
    void record(vk::CommandBuffer commandBuffer, vk::Extent2D extent,
                                const Push& push, vk::DescriptorSet textureSet) const;
    void destroy();

private:
    static constexpr vk::ShaderStageFlags PUSH_STAGES =
        vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment;

    vk::Device         device_         = nullptr;
    vk::PipelineLayout pipelineLayout_ = nullptr;
    vk::Pipeline       pipeline_       = nullptr;

    PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount_ = nullptr;
};

/* ------------------------------------------------------------------------ */

template <typename Push>
bool DrawPass<Push>::init(vk::Device device, ShaderLoader& shaderLoader,
                          const std::filesystem::path& meshShaderPath,
                          const std::filesystem::path& fragmentShaderPath,
                          vk::Format colorFormat, vk::Format depthFormat,
                          PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount,
                          vk::DescriptorSetLayout textureLayout, bool disableDepthTest, bool disableDepthWrite) {
    if (device_) {
        logError("DrawPass: init called twice");
        return false;
    }
    if (!drawIndirectCount) {
        logError("DrawPass: vkCmdDrawMeshTasksIndirectCountEXT is required");
        return false;
    }

    device_            = device;
    drawIndirectCount_ = drawIndirectCount;

    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = PUSH_STAGES;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(Push);

    //Setup pipeline
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    /* Set 0 is the bindless texture array. Null layout leaves the pipeline with no set */
    layoutInfo.setLayoutCount         = textureLayout ? 1u : 0u;
    layoutInfo.pSetLayouts            = &textureLayout;
    if (device_.createPipelineLayout(&layoutInfo, nullptr, &pipelineLayout_) != vk::Result::eSuccess) {
        logError("DrawPass: vkCreatePipelineLayout failed");
        destroy();
        return false;
    }

    vk::ShaderModule meshShader = shaderLoader.load(meshShaderPath);
    vk::ShaderModule fragShader = shaderLoader.load(fragmentShaderPath);
    if (meshShader && fragShader) {
        pipeline_ = createMeshPipeline(device_, pipelineLayout_, meshShader, fragShader,
                                       colorFormat, depthFormat, disableDepthTest, disableDepthWrite);
    } else {
        logError("DrawPass: failed to load " + meshShaderPath.string() + " / " +
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
template <typename Push>
void DrawPass<Push>::record(vk::CommandBuffer commandBuffer, vk::Extent2D extent,
                            const Push& push, vk::DescriptorSet textureSet) const {

    if (!pipeline_) return;

    const auto [viewport, scissor] = viewportAndScissor(extent);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_);
    if (textureSet)
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout_, 0, 1, &textureSet, 0, nullptr);
    commandBuffer.setViewport(0, 1, &viewport);
    commandBuffer.setScissor(0, 1, &scissor);
    commandBuffer.pushConstants(pipelineLayout_, PUSH_STAGES, 0, sizeof(push), &push);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

template <typename Push>
void DrawPass<Push>::record(vk::CommandBuffer commandBuffer, vk::Extent2D extent,
                            const Push& push,
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
    commandBuffer.pushConstants(pipelineLayout_, PUSH_STAGES, 0, sizeof(push), &push);

    drawIndirectCount_(static_cast<VkCommandBuffer>(commandBuffer),
                       static_cast<VkBuffer>(commands.buffer), commands.offset,
                       static_cast<VkBuffer>(count.buffer), count.offset,
                       maxDrawCount, sizeof(GPUMeshTaskCommand));
}

template <typename Push>
void DrawPass<Push>::destroy() {
    if (!device_) return;

    device_.destroyPipeline(pipeline_);
    device_.destroyPipelineLayout(pipelineLayout_);

    pipeline_          = nullptr;
    pipelineLayout_    = nullptr;
    device_            = nullptr;
    drawIndirectCount_ = nullptr;
}
