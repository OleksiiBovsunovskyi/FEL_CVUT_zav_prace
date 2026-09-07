module;
#include <vulkan/vulkan.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>

#include <glm/glm.hpp>

module Mesh;

import Logger;
import VkUtil;

namespace {

/* Every section starts on this boundary, so a pointer into the blob satisfies
 * the alignment of the record it addresses. */
constexpr vk::DeviceSize SECTION_ALIGNMENT = 16;

constexpr vk::DeviceSize alignUp(vk::DeviceSize value) {
    return (value + SECTION_ALIGNMENT - 1) & ~(SECTION_ALIGNMENT - 1);
}

/**
 * Byte offsets of one mesh's sections inside its blob. Built before anything is
 * allocated, so a rejected mesh costs nothing.
 */
struct BlobLayout {
    vk::DeviceSize meshlets             = 0;
    vk::DeviceSize meshletVertexIndices = 0;
    vk::DeviceSize meshletTriangles     = 0;
    vk::DeviceSize vertices             = 0;
    vk::DeviceSize size                 = 0;
};

BlobLayout layoutOf(const MeshUploadData& data) {
    BlobLayout layout{};
    vk::DeviceSize cursor = alignUp(sizeof(GPUMeshHeader));

    layout.meshlets = cursor;
    cursor = alignUp(cursor + data.meshlets.size() * sizeof(GPUMeshlet));

    layout.meshletVertexIndices = cursor;
    cursor = alignUp(cursor + data.meshletVertexIndices.size() *
                                  sizeof(GPUMeshletVertexIndex));

    layout.meshletTriangles = cursor;
    cursor = alignUp(cursor + data.meshletTriangles.size() *
                                  sizeof(GPUMeshletTriangle));

    layout.vertices = cursor;
    cursor = alignUp(cursor + data.vertices.size() * sizeof(GPUVertex));

    layout.size = cursor;
    return layout;
}

/// Copies one section into the blob image being built in the upload buffer.
template <typename T>
void writeSection(std::byte* blob, vk::DeviceSize offset,
                  std::span<const T> values) {
    if (values.empty()) return;
    std::memcpy(blob + offset, values.data(), values.size() * sizeof(T));
}

} // namespace

MeshDataPointer Mesh::header() const {
    if (!blob_) {
        logError("Mesh::header called before the mesh was uploaded");
        return {};
    }
    /* The header is the first record in the blob, so the blob's address is it. */
    return MeshDataPointer{blob_.span().gpu.data.address};
}

bool Mesh::upload(BufferManager& buffers, vk::CommandBuffer commandBuffer,
                  const MeshUploadData& data,
                  std::shared_ptr<Material> material) {

    if (!buffers.initialized()) {
        logError("Mesh::upload called before BufferManager::init");
        return false;
    }
    if (uploaded() || !material || data.vertices.empty() ||
        data.meshlets.empty() || data.meshletVertexIndices.empty() ||
        data.meshletTriangles.empty()) {
        return false;
    }

    /* Material first: its record has to exist before the header names it. */
    MappedSpan<std::byte> materialUpload{};
    const bool uploadMaterial = !material->uploaded();
    if (uploadMaterial && !material->prepare(buffers, materialUpload))
        return false;

    const BlobLayout layout = layoutOf(data);

    DeviceArray<std::byte> blob =
        buffers.allocateStatic<StaticBufferKind::MeshData>(
            static_cast<uint32_t>(layout.size), SECTION_ALIGNMENT);
    if (!blob) return false;

    const MappedSpan<std::byte> upload =
        buffers.allocateUpload(layout.size, SECTION_ALIGNMENT);
    if (!upload) return false;

    /* The blob is assembled in the upload buffer, pointers and all, then moved
     * across in one copy. */
    const vk::DeviceAddress base = blob.span().gpu.data.address;

    GPUMeshHeader header{};
    header.vertices = GpuPtr<GPUVertex>{base + layout.vertices};
    header.meshlets = GpuPtr<GPUMeshlet>{base + layout.meshlets};
    header.meshletVertexIndices =
        GpuPtr<GPUMeshletVertexIndex>{base + layout.meshletVertexIndices};
    header.meshletTriangles =
        GpuPtr<GPUMeshletTriangle>{base + layout.meshletTriangles};
    header.vertexCount    = static_cast<uint32_t>(data.vertices.size());
    header.meshletCount   = static_cast<uint32_t>(data.meshlets.size());
    header.material       = material->gpuIndex();
    header.boundingSphere = glm::vec4(data.bounds.center, data.bounds.radius);

    std::byte* image = upload.host;
    std::memset(image, 0, layout.size);
    std::memcpy(image, &header, sizeof(header));
    writeSection(image, layout.meshlets, data.meshlets);
    writeSection(image, layout.vertices, data.vertices);

    /* The two index arrays are single-field records over the generator's
     * uint32 output, so they copy as raw bytes. */
    std::memcpy(image + layout.meshletVertexIndices,
                data.meshletVertexIndices.data(),
                data.meshletVertexIndices.size() * sizeof(uint32_t));
    std::memcpy(image + layout.meshletTriangles, data.meshletTriangles.data(),
                data.meshletTriangles.size() * sizeof(uint32_t));

    if (uploadMaterial) material->record(commandBuffer, materialUpload);

    copy(commandBuffer, upload.region, blob.span().region);

    const std::array<BufferRegion, 1> written{blob.span().region};
    barrier(commandBuffer, written,
            vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eTaskShaderEXT |
                vk::PipelineStageFlagBits2::eMeshShaderEXT |
                vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderStorageRead);

    blob_         = std::move(blob);
    material_     = std::move(material);
    bounds_       = data.bounds;
    vertexCount_  = header.vertexCount;
    meshletCount_ = header.meshletCount;
    return true;
}
