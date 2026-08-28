module;
#include <vulkan/vulkan.h>

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

namespace {

/// Reserved and filled on the host; copy not recorded yet.
struct Staged {
    BufferAllocation destination;
    BufferSlice      staging;
};

/// Number of ranges a mesh writes: six geometry arrays plus its own record.
constexpr size_t STAGED_COUNT = 7;

/**
 * Reserves `values.size()` records and copies them into staging. Records
 * nothing; the caller decides whether the batch goes ahead.
 */
template <typename T>
bool stageRange(VK_buffers& buffers, StaticBufferKind kind,
                std::span<const T> values, Staged& out) {
    if (values.empty()) return false;

    BufferAllocation destination =
        buffers.allocateStatic(kind, static_cast<uint32_t>(values.size()));
    if (!destination) return false;

    const std::span<const std::byte> bytes = std::as_bytes(values);

    /* Trips only if the stride table and T disagree. */
    if (destination.slice().size != bytes.size()) {
        logError("Mesh: static buffer stride does not match the staged record");
        return false;
    }

    const BufferSlice staging = buffers.allocateUpload(bytes.size(), 16);
    if (!staging || !staging.mapped) return false;

    std::memcpy(staging.mapped, bytes.data(), bytes.size());

    out.destination = std::move(destination);
    out.staging     = staging;
    return true;
}

/// Records every staged copy, then one barrier covering all of them.
void recordStaged(VkCommandBuffer commandBuffer,
                  const std::array<Staged, STAGED_COUNT>& staged) {
    for (const Staged& range : staged) {
        const VkBufferCopy copy{
            .srcOffset = range.staging.offset,
            .dstOffset = range.destination.slice().offset,
            .size      = range.destination.slice().size,
        };
        vkCmdCopyBuffer(commandBuffer, range.staging.buffer,
                        range.destination.slice().buffer, 1, &copy);
    }

    std::array<VkBufferMemoryBarrier2, STAGED_COUNT> barriers{};
    for (size_t i = 0; i < STAGED_COUNT; ++i) {
        const BufferSlice& slice = staged[i].destination.slice();
        barriers[i] = VkBufferMemoryBarrier2{
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                             VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = slice.buffer,
            .offset = slice.offset,
            .size   = slice.size,
        };
    }

    const VkDependencyInfo dependency{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
        .pBufferMemoryBarriers    = barriers.data(),
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

} // namespace

uint32_t Mesh::getGpuIndex() const {
    if (!gpuRecord_) {
        logError("Mesh::getGpuIndex called before the mesh was uploaded");
        return INVALID_GPU_MESH_INDEX;
    }
    return gpuRecord_.slice().elementIndex;
}

bool Mesh::upload(VK_buffers& buffers, VkCommandBuffer commandBuffer,
                  const MeshUploadData& data,
                  std::shared_ptr<Material> material) {

    if (!buffers.initialized()) {
        logError("Mesh::upload called before VK_buffers::init");
        return false;
    }

    if (uploaded() || !material || data.vertices.empty() ||
        data.meshlets.empty() || data.meshletVertexIndices.empty() ||
        data.meshletTriangles.empty() || data.clusters.empty() ||
        data.clusterGroups.empty()) {
        return false;
    }

    /**
     * Nothing below touches the command buffer until every stage succeeds.
     * Material first: staging reserves its slot, making gpuIndex() valid for the
     * GPUMesh built below while its copy is recorded later.
     */
    BufferSlice materialStaging{};
    const bool stageMaterial = !material->uploaded();
    if (stageMaterial && !material->stage(buffers, materialStaging))
        return false;

    enum : size_t {
        VERTICES, MESHLETS, MESHLET_VERTEX_INDICES, MESHLET_TRIANGLES,
        CLUSTERS, CLUSTER_GROUPS, GPU_RECORD,
    };

    std::array<Staged, STAGED_COUNT> staged{};

    if (!stageRange(buffers, StaticBufferKind::Vertices,
                    data.vertices, staged[VERTICES]) ||
        !stageRange(buffers, StaticBufferKind::Meshlets,
                    data.meshlets, staged[MESHLETS]) ||
        !stageRange(buffers, StaticBufferKind::MeshletVertexIndices,
                    data.meshletVertexIndices, staged[MESHLET_VERTEX_INDICES]) ||
        !stageRange(buffers, StaticBufferKind::MeshletTriangleIndices,
                    data.meshletTriangles, staged[MESHLET_TRIANGLES]) ||
        !stageRange(buffers, StaticBufferKind::Clusters,
                    data.clusters, staged[CLUSTERS]) ||
        !stageRange(buffers, StaticBufferKind::ClusterGroups,
                    data.clusterGroups, staged[CLUSTER_GROUPS])) {
        return false;
    }

    GPUMesh gpuMesh{};
    gpuMesh.vertexOffset = staged[VERTICES].destination.slice().elementIndex;
    gpuMesh.vertexCount  = static_cast<uint32_t>(data.vertices.size());
    gpuMesh.meshletOffset = staged[MESHLETS].destination.slice().elementIndex;
    gpuMesh.meshletCount  = static_cast<uint32_t>(data.meshlets.size());
    gpuMesh.meshletVertexIndexOffset =
        staged[MESHLET_VERTEX_INDICES].destination.slice().elementIndex;
    gpuMesh.meshletTriangleOffset =
        staged[MESHLET_TRIANGLES].destination.slice().elementIndex;
    gpuMesh.materialIndex = material->gpuIndex();
    gpuMesh.clusterOffset = staged[CLUSTERS].destination.slice().elementIndex;
    gpuMesh.clusterCount  = static_cast<uint32_t>(data.clusters.size());
    gpuMesh.clusterGroupOffset =
        staged[CLUSTER_GROUPS].destination.slice().elementIndex;
    gpuMesh.clusterGroupCount =
        static_cast<uint32_t>(data.clusterGroups.size());
    gpuMesh.boundingSphere = glm::vec4(data.bounds.center, data.bounds.radius);

    const std::span<const GPUMesh> record{&gpuMesh, 1};
    if (!stageRange(buffers, StaticBufferKind::Meshes, record,
                    staged[GPU_RECORD])) {
        return false;
    }

    if (stageMaterial)
        material->record(commandBuffer, materialStaging);

    recordStaged(commandBuffer, staged);

    vertices_             = std::move(staged[VERTICES].destination);
    meshlets_             = std::move(staged[MESHLETS].destination);
    meshletVertexIndices_ = std::move(staged[MESHLET_VERTEX_INDICES].destination);
    meshletTriangles_     = std::move(staged[MESHLET_TRIANGLES].destination);
    clusters_             = std::move(staged[CLUSTERS].destination);
    clusterGroups_        = std::move(staged[CLUSTER_GROUPS].destination);
    gpuRecord_            = std::move(staged[GPU_RECORD].destination);
    material_             = std::move(material);
    bounds_               = data.bounds;
    vertexCount_          = static_cast<uint32_t>(data.vertices.size());
    meshletCount_         = static_cast<uint32_t>(data.meshlets.size());
    return true;
}
