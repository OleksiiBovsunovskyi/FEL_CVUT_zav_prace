module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module AssetData;

export import MeshData;
export import ClusterLODGenerator;

/// A VkFormat, held as its number so this module needs no Vulkan header.
export struct AssetFormat {
    uint32_t value = 0;
    [[nodiscard]] bool operator==(const AssetFormat&) const = default;
};

/// A slot in AssetModel::materials.
export struct AssetMaterialIndex {
    static constexpr uint32_t NONE = std::numeric_limits<uint32_t>::max();
    uint32_t value = NONE;
    [[nodiscard]] explicit operator bool() const { return value != NONE; }
};

/// A slot in AssetModel::textures.
export struct AssetTextureIndex {
    static constexpr uint32_t NONE = std::numeric_limits<uint32_t>::max();
    uint32_t value = NONE;
    [[nodiscard]] explicit operator bool() const { return value != NONE; }
};

/// A slot in AssetModel::meshes.
export struct AssetMeshIndex {
    uint32_t value = 0;
};

/// Albedo, normal, ORM and emissive, in the order GPUMaterial::textures holds them.
export constexpr size_t ASSET_TEXTURE_SLOTS = 4;

/**
 * One image and its whole mip chain.
 *
 * @note Levels are tightly packed largest first;
 */
export struct AssetTexture {
    uint32_t    width      = 0;
    uint32_t    height     = 0;
    AssetFormat format{};
    uint32_t    levelCount = 1;
    std::span<const std::byte> pixels;
};

/// Material parameters in the form Material's setters take.
export struct AssetMaterial {
    glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissiveColor{0.0f};
    float     emissiveIntensity = 1.0f;
    float     specularIntensity = 0.5f;
    float     shininess         = 32.0f;
    float     metallic          = 0.0f;
    float     roughness         = 0.1f;
    float     alphaThreshold    = 0.0f;
    /// MaterialFlags.
    uint32_t  flags             = 0;
    std::array<AssetTextureIndex, ASSET_TEXTURE_SLOTS> textures{};
};

/// One primitive's geometry and the material it draws with.
export struct AssetMesh {
    MeshUploadData     geometry;
    AssetMaterialIndex material;
};

/// One placement of a mesh in the model's space.
export struct AssetPart {
    AssetMeshIndex mesh;
    glm::mat4      transform{1.0f};
};

/**
 * One model, as spans over storage somebody else owns
 */
export struct AssetModel {
    std::span<const AssetMesh>     meshes;
    std::span<const AssetMaterial> materials;
    std::span<const AssetTexture>  textures;
    std::span<const AssetPart>     parts;

    [[nodiscard]] bool empty() const { return parts.empty(); }
};

/// @return levels down to 1x1.
export [[nodiscard]] uint32_t assetLevelCount(uint32_t width, uint32_t height);

/**
 * Bytes one mip level occupies
 *
 * @param format one of the RGBA8 or BC7 formats the baker writes.
 * @param width level width in texels.
 * @param height level height in texels.
 * @return 0 for a format this does not know.
 */
export [[nodiscard]] size_t assetLevelBytes(AssetFormat format, uint32_t width,
                                            uint32_t height);

/**
 * Owns one model's geometry, pixels and tables, and hands out an AssetModel
 * over them.
 */
export class ImportedAsset {
public:
    /// One mesh's owned geometry, in the form generateClusterLOD produces.
    struct MeshStorage {
        std::vector<GPUVertex> vertices;
        GeneratedClusterLOD    lod;
        MeshBounds             bounds;
        AssetMaterialIndex     material;
    };

    /// One texture's owned pixels
    struct TextureStorage {
        uint32_t               width      = 0;
        uint32_t               height     = 0;
        AssetFormat            format{};
        uint32_t               levelCount = 1;
        std::vector<std::byte> pixels;
    };

    std::vector<MeshStorage>    meshes;
    std::vector<AssetMaterial>  materials;
    std::vector<TextureStorage> textures;
    std::vector<AssetPart>      parts;

    [[nodiscard]] bool empty() const { return parts.empty(); }

    /// @return spans over this object's storage.
    [[nodiscard]] AssetModel view();

private:
    std::vector<AssetMesh>    meshViews_;
    std::vector<AssetTexture> textureViews_;
};
