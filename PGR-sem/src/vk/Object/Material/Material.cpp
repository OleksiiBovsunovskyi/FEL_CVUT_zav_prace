module;
#include <vulkan/vulkan.h>

#include <cstring>

#include <glm/glm.hpp>

module VK_Material;

GPUMaterial Material::gpuData() const {
    GPUMaterial result{};

    result.albedo = albedo_;

    result.emissive = glm::vec4(emissiveColor_, emissiveIntensity_);

    result.surface = glm::vec4(specularIntensity_, shininess_, metallic_, roughness_);

    result.textures = glm::uvec4(albedoTexture_, normalTexture_,
                                 ormTexture_, emissiveTexture_);

    result.alphaThreshold = alphaThreshold_;
    if (emissive_) result.flags |= MATERIAL_EMISSIVE;
    if (transparent_) result.flags |= MATERIAL_ALPHA_BLEND;
    if (alphaThreshold_ > 0.0f) result.flags |= MATERIAL_ALPHA_MASK;

    return result;
}

uint32_t Material::gpuIndex() const {
    if (!gpuRecord_) return 0;
    return gpuRecord_.slice().elementIndex;
}

bool Material::stage(VK_buffers& buffers, BufferSlice& outStaging) {
    if (!gpuRecord_) {
        gpuRecord_ = buffers.allocateStatic(StaticBufferKind::Materials, 1);
        if (!gpuRecord_) return false;
    }

    const BufferSlice staging =
        buffers.allocateUpload(sizeof(GPUMaterial), alignof(GPUMaterial));
    if (!staging || !staging.mapped) return false;

    const GPUMaterial data = gpuData();
    std::memcpy(staging.mapped, &data, sizeof(data));

    outStaging = staging;
    return true;
}

void Material::record(VkCommandBuffer commandBuffer,
                      const BufferSlice& staging) const {
    const VkBufferCopy copy{
        .srcOffset = staging.offset,
        .dstOffset = gpuRecord_.slice().offset,
        .size      = sizeof(GPUMaterial),
    };
    vkCmdCopyBuffer(commandBuffer, staging.buffer,
                    gpuRecord_.slice().buffer, 1, &copy);

    const VkBufferMemoryBarrier2 barrier{
        .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = gpuRecord_.slice().buffer,
        .offset = gpuRecord_.slice().offset,
        .size   = gpuRecord_.slice().size,
    };
    const VkDependencyInfo dependency{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &barrier,
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

bool Material::upload(VK_buffers& buffers, VkCommandBuffer commandBuffer) {
    BufferSlice staging{};
    if (!stage(buffers, staging)) return false;
    record(commandBuffer, staging);
    return true;
}
