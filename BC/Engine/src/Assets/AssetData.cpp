module;

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include <vulkan/vulkan_core.h>

module AssetData;

uint32_t assetLevelCount(uint32_t width, uint32_t height) {
    const uint32_t largest = std::max(width, height);
    return largest == 0 ? 1u : std::bit_width(largest);
}

size_t assetLevelBytes(AssetFormat format, uint32_t width, uint32_t height) {
    switch (format.value) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return size_t{width} * height * 4;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return size_t{(width + 3) / 4} * ((height + 3) / 4) * 16;
        default:
            return 0;
    }
}

AssetModel ImportedAsset::view() {
    meshViews_.clear();
    meshViews_.reserve(meshes.size());
    for (const MeshStorage& mesh : meshes)
        meshViews_.push_back(
            AssetMesh{mesh.lod.uploadData(mesh.vertices, mesh.bounds), mesh.material});

    textureViews_.clear();
    textureViews_.reserve(textures.size());
    for (const TextureStorage& texture : textures)
        textureViews_.push_back(AssetTexture{texture.width, texture.height,
                                             texture.format, texture.levelCount,
                                             texture.pixels});

    return AssetModel{meshViews_, materials, textureViews_, parts};
}
