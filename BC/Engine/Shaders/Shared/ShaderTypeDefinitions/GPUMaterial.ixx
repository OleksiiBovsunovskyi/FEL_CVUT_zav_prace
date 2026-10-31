module;

#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

export module GPUMaterial;
/**
 * Material records and the bindless texture slot sentinel.
 */

export constexpr uint32_t INVALID_TEXTURE_INDEX =
    std::numeric_limits<uint32_t>::max();

/// Record index into the Materials mega-buffer.
export struct GPUMaterialIndex {
    uint32_t value = 0;
};

static_assert(sizeof(GPUMaterialIndex) == 4);

export enum MaterialFlags : uint32_t {
    MATERIAL_EMISSIVE    = 1u << 0,
    MATERIAL_ALPHA_BLEND = 1u << 1,
    MATERIAL_ALPHA_MASK  = 1u << 2,
};

/// Material record; five 16-byte blocks.
export struct alignas(16) GPUMaterial {
    glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 emissive{0.0f, 0.0f, 0.0f, 1.0f};

    /* x=specular intensity, y=shininess, z=metallic, w=roughness */
    glm::vec4 surface{0.5f, 32.0f, 0.0f, 0.1f};

    /* Albedo, normal, ORM and emissive bindless descriptor indices. */
    glm::uvec4 textures{
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
        INVALID_TEXTURE_INDEX,
    };

    float      alphaThreshold = 0.0f;
    uint32_t   flags          = 0;
    glm::uvec2 _padding{};
};

static_assert(sizeof(GPUMaterial) == 80);
