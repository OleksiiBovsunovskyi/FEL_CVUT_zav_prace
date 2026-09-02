module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

module Pipeline;

import Logger;
import VkUtil;

VkPipelineShaderStageCreateInfo shaderStage(VkShaderStageFlagBits stage,
                                             VkShaderModule module) {
    VkPipelineShaderStageCreateInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage  = stage;
    info.module = module;
    info.pName  = "main";
    return info;
}

VkPipeline createComputePipeline(VkDevice device, VkPipelineLayout layout,
                                 VkShaderModule shader) {
    if (!device || !layout || !shader) {
        logError("createComputePipeline: device, layout and shader are required");
        return VK_NULL_HANDLE;
    }

    const VkPipelineShaderStageCreateInfo stage =
        shaderStage(VK_SHADER_STAGE_COMPUTE_BIT, shader);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage  = stage;
    pipelineInfo.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult r = vkCreateComputePipelines(
        device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        logError("createComputePipeline: vkCreateComputePipelines failed: VkResult " +
                 std::to_string(r));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

VkPipeline createMeshPipeline(VkDevice device, VkPipelineLayout layout,
                              VkShaderModule meshShader,
                              VkShaderModule fragmentShader,
                              VkFormat colorFormat, VkFormat depthFormat) {
    if (!device || !layout || !meshShader || !fragmentShader) {
        logError("createMeshPipeline: device, layout and both shaders are required");
        return VK_NULL_HANDLE;
    }

    const VkPipelineShaderStageCreateInfo stages[]{
        shaderStage(VK_SHADER_STAGE_MESH_BIT_EXT, meshShader),
        shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader),
    };

    /* A mesh pipeline has no vertex input or input assembly state at all. */
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_BACK_BIT;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    /* Reverse-Z: the far plane is 0, so a nearer fragment compares greater. */
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = DEPTH_COMPARE_OP;
    depthStencil.maxDepthBounds   = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttachment;

    /* The swapchain resizes, so neither is baked into the pipeline. */
    const VkDynamicState dynamicStates[]{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount    = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat   = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext               = &rendering;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pViewportState      = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState   = &multisample;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &blend;
    pipelineInfo.pDynamicState       = &dynamic;
    pipelineInfo.layout              = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult r = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (r != VK_SUCCESS) {
        logError("createMeshPipeline: vkCreateGraphicsPipelines failed: VkResult " +
                 std::to_string(r));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}
