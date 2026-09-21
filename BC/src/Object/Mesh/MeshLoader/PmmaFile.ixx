module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

export module PmmaFile;

export import AssetData;

/// Extension of a baked model.
export constexpr const char* PMMA_EXTENSION = ".pmma";

/**
 * Writes one model as a .pmma.
 *
 * @param path file to create, overwriting any existing one.
 * @param model geometry, materials, textures and parts to bake.
 * @return false after logging, leaving a partial file behind.
 */
export [[nodiscard]] bool writePmma(const std::filesystem::path& path,
                                    const AssetModel& model);

/**
 * One .pmma held in memory, and the AssetModel over it.
 */
export class PmmaAsset {
public:
    /**
     * @param path a .pmma.
     */
    [[nodiscard]] bool read(const std::filesystem::path& path);

    /// @return spans over this object's bytes. Empty until read() succeeds.
    [[nodiscard]] AssetModel model() const {
        return AssetModel{meshes_, materials_, textures_, parts_};
    }

private:
    std::vector<std::byte>     blob_;
    std::vector<AssetMesh>     meshes_;
    std::vector<AssetMaterial> materials_;
    std::vector<AssetTexture>  textures_;
    std::vector<AssetPart>     parts_;
};
