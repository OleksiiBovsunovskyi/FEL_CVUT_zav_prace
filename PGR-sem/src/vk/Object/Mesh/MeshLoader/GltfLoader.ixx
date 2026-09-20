module;
#include <filesystem>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

export module GltfLoader;

export import MultiMesh;
export import ClusterLODGenerator;

import BufferManager;
import BlockingTransferBatch;
import TextureManager;

export struct GltfLoadSettings {
    /**
     * Passed straight to generateClusterLOD. The default (enabled = false) builds
     * meshlets with one terminal group; the layout matches hierarchical mode.
     */
    ClusterLODSettings clusterLod{};
};

/**
 * Loads a .gltf or .glb as one model.
 *
 * Every scene node that references a mesh becomes one part, carrying that
 * node's world transform, because a glTF primitive is one geometry with one
 * material (a Mesh) and everything the file places is one thing to put in the
 * world. A single-primitive file still comes back as a one-part MultiMesh - no
 * special case.
 *
 * Node world transforms end up on the parts, so the result needs no further
 * hierarchy walking, and the Object that carries the model places all of it at
 * once.
 *
 * Geometry and materials are shared: two nodes on the same glTF mesh reference
 * one Mesh, and primitives with the same material index share one GPUMaterial
 * record.
 */
export class GltfLoader {
public:
    GltfLoader() = default;

    /**
     * @param buffers where geometry and materials are allocated; must outlive
     *        this.
     * @param batch used for every upload; must be initialized and never left
     *        open by a caller, since loadModel opens and submits it as it goes.
     * @param textures where decoded images land; must outlive this.
     */
    bool init(BufferManager& buffers, BlockingTransferBatch& batch,
              TextureManager& textures);

    /**
     * @param path a .gltf or .glb.
     * @return the model, or null on a parse error, an unsupported primitive or
     *         a failed upload. Whatever uploaded before the failure stays
     *         allocated until the returned parts die.
     */
    [[nodiscard]] std::shared_ptr<MultiMesh> loadModel(
        const std::filesystem::path& path,
        const GltfLoadSettings& settings = {});

private:
    BufferManager*         buffers_  = nullptr;
    BlockingTransferBatch* batch_    = nullptr;
    TextureManager*        textures_ = nullptr;
};
