module;
#include <filesystem>
#include <vector>

export module GltfLoader;

export import MultiMesh;
export import ClusterLODGenerator;

import VK_Buffers;
import UploadBatch;

export struct GltfLoadSettings {
    /**
     * Passed straight to generateClusterLOD. The default (enabled = false) builds
     * meshlets with one terminal group; the layout matches hierarchical mode.
     */
    ClusterLODSettings clusterLod{};
};

/**
 * Loads a .gltf or .glb into MultiMeshes.
 *
 * One MultiMesh per scene node that references a mesh, with one part per glTF
 * primitive, because that is how the two formats line up: a glTF primitive is
 * one geometry with one material (a Mesh), and a glTF mesh is a list of those (a
 * MultiMesh). A single-primitive mesh therefore still comes back as a one-part
 * MultiMesh - no special case, as requested.
 *
 * Part transforms are node *world* transforms; the returned vector renders the
 * whole scene with no further hierarchy walking.
 *
 * Geometry and materials are shared: two nodes on the same glTF mesh get parts
 * holding one shared_ptr<Mesh>, and primitives with the same material index
 * share one GPUMaterial record.
 *
 * `batch` must be initialized and not already open; the loader opens and
 * submits it as it goes.
 *
 * @return false on a parse error, an unsupported primitive, or a failed upload.
 *         `out` may hold already-loaded objects when it fails.
 */
export [[nodiscard]] bool loadGltf(const std::filesystem::path& path,
                                   VK_buffers& buffers,
                                   UploadBatch& batch,
                                   std::vector<MultiMesh>& out,
                                   const GltfLoadSettings& settings = {});
