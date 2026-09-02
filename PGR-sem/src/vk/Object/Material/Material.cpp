module;
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstring>

#include <glm/glm.hpp>

module VK_Material;

import Logger;
import VkUtil;

namespace {

/* A power of two that sizeof(GPUMaterial) is a multiple of, so consecutive
 * records pack without a gap and stay indexable. */
constexpr VkDeviceSize MATERIAL_ALIGNMENT = 16;

static_assert(sizeof(GPUMaterial) % MATERIAL_ALIGNMENT == 0);

} // namespace

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

GPUMaterialIndex Material::gpuIndex() const {
    if (!gpuRecord_) return {};

    const VkDeviceSize offset = gpuRecord_.span().region.offset;
    /* Records are allocated one at a time and sizeof(GPUMaterial) is a
     * multiple of the alignment, so every record lands on a record boundary. */
    if (offset % sizeof(GPUMaterial) != 0) {
        logError("Material: record is not on a record boundary; the Materials "
                 "buffer can no longer be indexed by record");
        return {};
    }
    return GPUMaterialIndex{static_cast<uint32_t>(offset / sizeof(GPUMaterial))};
}

bool Material::prepare(BufferManager& buffers, MappedSpan<std::byte>& outUpload) {
    if (!gpuRecord_) {
        gpuRecord_ =
            buffers.allocateStatic<StaticBufferKind::Materials>(1, MATERIAL_ALIGNMENT);
        if (!gpuRecord_) return false;
    }

    const MappedSpan<std::byte> upload =
        buffers.allocateUpload(sizeof(GPUMaterial), MATERIAL_ALIGNMENT);
    if (!upload) return false;

    const GPUMaterial data = gpuData();
    std::memcpy(upload.host, &data, sizeof(data));

    outUpload = upload;
    return true;
}

void Material::record(VkCommandBuffer commandBuffer,
                      const MappedSpan<std::byte>& upload) const {
    const BufferRegion destination = gpuRecord_.span().region;

    copy(commandBuffer, upload.region, destination);

    const std::array<BufferRegion, 1> written{destination};
    barrier(commandBuffer, written,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

bool Material::upload(BufferManager& buffers, VkCommandBuffer commandBuffer) {
    MappedSpan<std::byte> upload{};
    if (!prepare(buffers, upload)) return false;
    record(commandBuffer, upload);
    return true;
}
