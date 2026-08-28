module;
#include <vulkan/vulkan.h>

#include <span>

export module Pipeline;

import VkUtil;

/**
 * Graphics pipeline creation with this renderer's fixed choices: dynamic viewport
 * and scissor, no MSAA, no blending, no vertex input, no render pass.
 *
 * Naming a depthFormat enables depth test and write with the reverse-Z compare
 * op.
 */
export struct GraphicsPipelineDesc {
    std::span<const VkPipelineShaderStageCreateInfo> stages;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    /// VK_FORMAT_UNDEFINED for a pass with no depth attachment.
    VkFormat depthFormat = DEPTH_FORMAT;

    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
};

/**
 * @return VK_NULL_HANDLE on failure, after logging.
 */
export [[nodiscard]] VkPipeline createGraphicsPipeline(
    VkDevice device, const GraphicsPipelineDesc& desc);

/**
 * Creates a compute pipeline with the supplied layout.
 *
 * @return VK_NULL_HANDLE on failure, after logging.
 */
export [[nodiscard]] VkPipeline createComputePipeline(
    VkDevice device, VkPipelineLayout layout, VkShaderModule shader);

export [[nodiscard]] VkPipelineShaderStageCreateInfo shaderStage(
    VkShaderStageFlagBits stage, VkShaderModule module);
