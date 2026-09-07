module;
#include <vulkan/vulkan.hpp>

export module Pipeline;

/**
 * Creates a compute pipeline with the supplied layout.
 *
 * @return nullptr on failure, after logging.
 */
export [[nodiscard]] vk::Pipeline createComputePipeline(
    vk::Device device, vk::PipelineLayout layout, vk::ShaderModule shader);

export [[nodiscard]] vk::PipelineShaderStageCreateInfo shaderStage(
    vk::ShaderStageFlagBits stage, vk::ShaderModule module);

/**
 * Creates a mesh-shader graphics pipeline for dynamic rendering: no vertex
 * input, dynamic viewport and scissor, back-face culling, reverse-Z depth.
 *
 * @param colorFormat format of the single colour attachment.
 * @param depthFormat format of the depth attachment.
 * @return nullptr on failure, after logging.
 */
export [[nodiscard]] vk::Pipeline createMeshPipeline(
    vk::Device device, vk::PipelineLayout layout,
    vk::ShaderModule meshShader, vk::ShaderModule fragmentShader,
    vk::Format colorFormat, vk::Format depthFormat);
