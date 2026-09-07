module;
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <string>

module Pipeline;

import Logger;
import VkUtil;

vk::PipelineShaderStageCreateInfo shaderStage(vk::ShaderStageFlagBits stage,
                                               vk::ShaderModule module) {
    vk::PipelineShaderStageCreateInfo info{};
    info.stage  = stage;
    info.module = module;
    info.pName  = "main";
    return info;
}

vk::Pipeline createComputePipeline(vk::Device device, vk::PipelineLayout layout,
                                   vk::ShaderModule shader) {
    if (!device || !layout || !shader) {
        logError("createComputePipeline: device, layout and shader are required");
        return nullptr;
    }

    const vk::PipelineShaderStageCreateInfo stage =
        shaderStage(vk::ShaderStageFlagBits::eCompute, shader);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage  = stage;
    pipelineInfo.layout = layout;

    vk::Pipeline pipeline = nullptr;
    const vk::Result r = device.createComputePipelines(
        nullptr, 1, &pipelineInfo, nullptr, &pipeline);
    if (r != vk::Result::eSuccess) {
        logError("createComputePipeline: createComputePipelines failed: " +
                 vk::to_string(r));
        return nullptr;
    }
    return pipeline;
}

vk::Pipeline createMeshPipeline(vk::Device device, vk::PipelineLayout layout,
                                vk::ShaderModule meshShader,
                                vk::ShaderModule fragmentShader,
                                vk::Format colorFormat, vk::Format depthFormat) {
    if (!device || !layout || !meshShader || !fragmentShader) {
        logError("createMeshPipeline: device, layout and both shaders are required");
        return nullptr;
    }

    const vk::PipelineShaderStageCreateInfo stages[]{
        shaderStage(vk::ShaderStageFlagBits::eMeshEXT, meshShader),
        shaderStage(vk::ShaderStageFlagBits::eFragment, fragmentShader),
    };

    /* A mesh pipeline has no vertex input or input assembly state at all. */
    vk::PipelineViewportStateCreateInfo viewport{};
    viewport.viewportCount = 1;
    viewport.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo raster{};
    raster.polygonMode = vk::PolygonMode::eFill;
    raster.cullMode    = vk::CullModeFlagBits::eBack;
    raster.frontFace   = vk::FrontFace::eCounterClockwise;
    raster.lineWidth   = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    /* Reverse-Z: the far plane is 0, so a nearer fragment compares greater. */
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::True;
    depthStencil.depthWriteEnable = vk::True;
    depthStencil.depthCompareOp   = DEPTH_COMPARE_OP;
    depthStencil.maxDepthBounds   = 1.0f;

    vk::PipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttachment;

    /* The swapchain resizes, so neither is baked into the pipeline. */
    const vk::DynamicState dynamicStates[]{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    vk::PipelineDynamicStateCreateInfo dynamic{};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamicStates;

    vk::PipelineRenderingCreateInfo rendering{};
    rendering.colorAttachmentCount    = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat   = depthFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
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

    vk::Pipeline pipeline = nullptr;
    const vk::Result r = device.createGraphicsPipelines(
        nullptr, 1, &pipelineInfo, nullptr, &pipeline);
    if (r != vk::Result::eSuccess) {
        logError("createMeshPipeline: createGraphicsPipelines failed: " +
                 vk::to_string(r));
        return nullptr;
    }
    return pipeline;
}
