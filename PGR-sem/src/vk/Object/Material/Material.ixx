module;
#include <vulkan/vulkan.h>

#include <cstdint>

#include <glm/glm.hpp>

export module VK_Material;

export import GPUTypes;
import VK_Buffers;

/**
 * One fixed-size material record in the Materials mega-buffer. Texture indices
 * point into the renderer's bindless texture array.
 */
export class Material {
public:
    Material() = default;
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) noexcept = default;
    Material& operator=(Material&&) noexcept = default;

    /// Allocates on first call, then updates the same record in-place.
    bool upload(VK_buffers& buffers, VkCommandBuffer commandBuffer);

    /**
     * First half of upload(): reserves the record slot if absent, then writes
     * current state into a staging slice. Records nothing, so abandoning the
     * batch after this leaves the command buffer untouched.
     */
    [[nodiscard]] bool stage(VK_buffers& buffers, BufferSlice& outStaging);

    /// Second half of upload(): records the copy staged by stage().
    void record(VkCommandBuffer commandBuffer, const BufferSlice& staging) const;

    [[nodiscard]] bool uploaded() const {
        return static_cast<bool>(gpuRecord_);
    }
    [[nodiscard]] uint32_t gpuIndex() const;
    [[nodiscard]] const BufferSlice& gpuSlice() const {
        return gpuRecord_.slice();
    }
    [[nodiscard]] GPUMaterial gpuData() const;

    void setAlbedo(const glm::vec4& value) { albedo_ = value; }
    void setEmissiveColor(const glm::vec3& value) { emissiveColor_ = value; }
    void setEmissiveIntensity(float value) { emissiveIntensity_ = value; }
    void setSpecularIntensity(float value) { specularIntensity_ = value; }
    void setShininess(float value) { shininess_ = value; }
    void setMetallic(float value) { metallic_ = value; }
    void setRoughness(float value) { roughness_ = value; }
    void setAlphaThreshold(float value) { alphaThreshold_ = value; }
    void setEmissive(bool value) { emissive_ = value; }
    void setTransparent(bool value) { transparent_ = value; }

    void setAlbedoTexture(uint32_t index) { albedoTexture_ = index; }
    void setNormalTexture(uint32_t index) { normalTexture_ = index; }
    void setORMTexture(uint32_t index) { ormTexture_ = index; }
    void setEmissiveTexture(uint32_t index) { emissiveTexture_ = index; }

    [[nodiscard]] const glm::vec4& albedo() const { return albedo_; }
    [[nodiscard]] const glm::vec3& emissiveColor() const {
        return emissiveColor_;
    }
    [[nodiscard]] float emissiveIntensity() const {
        return emissiveIntensity_;
    }
    [[nodiscard]] float specularIntensity() const {
        return specularIntensity_;
    }
    [[nodiscard]] float shininess() const { return shininess_; }
    [[nodiscard]] float metallic() const { return metallic_; }
    [[nodiscard]] float roughness() const { return roughness_; }
    [[nodiscard]] float alphaThreshold() const { return alphaThreshold_; }
    [[nodiscard]] bool isEmissive() const { return emissive_; }
    [[nodiscard]] bool isTransparent() const { return transparent_; }

private:
    BufferAllocation gpuRecord_{};

    glm::vec4 albedo_{1.0f};
    glm::vec3 emissiveColor_{0.0f};
    float emissiveIntensity_ = 1.0f;
    float specularIntensity_ = 0.5f;
    float shininess_         = 32.0f;
    float metallic_          = 0.0f;
    float roughness_         = 0.1f;
    float alphaThreshold_    = 0.0f;
    bool  emissive_          = false;
    bool  transparent_       = false;

    uint32_t albedoTexture_   = INVALID_TEXTURE_INDEX;
    uint32_t normalTexture_   = INVALID_TEXTURE_INDEX;
    uint32_t ormTexture_      = INVALID_TEXTURE_INDEX;
    uint32_t emissiveTexture_ = INVALID_TEXTURE_INDEX;
};
