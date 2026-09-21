module;
#include <filesystem>
#include <memory>

export module ModelLoader;

export import MultiMesh;
export import PmmaFile;

import BufferManager;
import BlockingTransferBatch;
import TextureManager;

/**
 *Loads pmma model
 */
export class ModelLoader {
public:
    ModelLoader() = default;

    /**
     * @param buffers where geometry and materials are allocated; must outlive
     *        this.
     * @param batch used for every upload; must be initialized and never left
     *        open by a caller.
     * @param textures where baked images land; must outlive this.
     */
    bool init(BufferManager& buffers, BlockingTransferBatch& batch,
              TextureManager& textures);

    /**
     * @param path a .pmma to load.
     * @return the model, or null on a failed upload.
     * @note Whatever uploaded before the failure stays allocated until the returned parts die.
     */
    [[nodiscard]] std::shared_ptr<MultiMesh> loadModel(
        const std::filesystem::path& path);

    /**
     * @param model geometry, materials and textures to upload.
     * @return the model, or null on a failed upload.
     */
    [[nodiscard]] std::shared_ptr<MultiMesh> upload(const AssetModel& model);

private:
    BufferManager*         buffers_  = nullptr;
    BlockingTransferBatch* batch_    = nullptr;
    TextureManager*        textures_ = nullptr;
};
