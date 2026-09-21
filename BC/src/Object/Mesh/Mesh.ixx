module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <glm/glm.hpp>

export module Mesh;

import vulkan;
export import MeshData;
export import VK_Material;
import BufferManager;

/**
 * One geometry primitive using one material.
 *
 * Owns a persistent allocation in every static geometry mega-buffer; destroying
 * or replacing it retires them through BufferManager. Transforms, visibility and
 * scene hierarchy live in render/transform components.
 */
export class Mesh {
public:
    /// Creates an empty, not-yet-uploaded mesh.
    Mesh() = default;

    /// GPU allocations are uniquely owned.
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    /// Transfers ownership of all GPU allocations.
    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    /**
     * Allocates the mesh's MeshData blob and records the copy into it.
     *
     * The command buffer must be recording and BufferManager must be initialized.
     * The material is uploaded automatically if needed.
     * Calling it on an uploaded Mesh returns false.
     *
     * All-or-nothing: every allocation and host copy precedes the first vkCmd*
     * call. A failed call still consumes upload space until the next
     * BufferManager::resetUpload().
     *
     * @return true when every allocation and copy was recorded.
     */
    bool upload(BufferManager& buffers, vk::CommandBuffer commandBuffer,
                const MeshUploadData& data,
                std::shared_ptr<Material> material);

    /// @return true once the mesh's blob is on the GPU.
    [[nodiscard]] bool uploaded() const {
        return static_cast<bool>(blob_);
    }

    /**
     * Call only after uploaded().
     * @return address of this mesh's GPUMeshHeader, or a null pointer before
     *         upload.
     */
    [[nodiscard]] MeshDataPointer header() const;

    /// @return the mesh's local-space bounding sphere.
    [[nodiscard]] const MeshBounds& bounds() const { return bounds_; }

    /// Returns the number of vertices owned by this mesh.
    [[nodiscard]] uint32_t vertexCount() const { return vertexCount_; }

    /// @return meshlet count across every hierarchy depth.
    [[nodiscard]] uint32_t meshletCount() const { return meshletCount_; }

    /// Returns the mesh's material. Requires uploaded() == true.
    [[nodiscard]] const Material& material() const { return *material_; }

    /// Returns shared ownership of the material. Empty before upload.
    [[nodiscard]] const std::shared_ptr<Material>& materialPtr() const {
        return material_;
    }

private:
    /// Header, meshlets, both index arrays and vertices, in one range.
    DeviceArray<std::byte> blob_{};

    std::shared_ptr<Material> material_;
    MeshBounds bounds_{};
    uint32_t vertexCount_  = 0;
    uint32_t meshletCount_ = 0;
};
