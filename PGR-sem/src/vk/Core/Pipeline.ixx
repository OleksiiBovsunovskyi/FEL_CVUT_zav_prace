module;
#include <vulkan/vulkan.h>

export module Pipeline;

/**
 * Creates a compute pipeline with the supplied layout.
 *
 * @return VK_NULL_HANDLE on failure, after logging.
 */
export [[nodiscard]] VkPipeline createComputePipeline(
    VkDevice device, VkPipelineLayout layout, VkShaderModule shader);

export [[nodiscard]] VkPipelineShaderStageCreateInfo shaderStage(
    VkShaderStageFlagBits stage, VkShaderModule module);

/**
 * Creates a mesh-shader graphics pipeline for dynamic rendering: no vertex
 * input, dynamic viewport and scissor, back-face culling, reverse-Z depth.
 *
 * @param colorFormat format of the single colour attachment.
 * @param depthFormat format of the depth attachment.
 * @return VK_NULL_HANDLE on failure, after logging.
 */
export [[nodiscard]] VkPipeline createMeshPipeline(
    VkDevice device, VkPipelineLayout layout,
    VkShaderModule meshShader, VkShaderModule fragmentShader,
    VkFormat colorFormat, VkFormat depthFormat);
