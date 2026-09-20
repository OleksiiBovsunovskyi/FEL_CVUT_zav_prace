module;

#include "glm/glm.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

module MeshDrawResources;

import vulkan;
import vk_mem_alloc;
import Logger;
import VkUtil;

namespace {

constexpr vk::BufferUsageFlags MESH_INSTANCES_USAGE = GPU_DATA_USAGE;
constexpr vk::BufferUsageFlags MESH_INSTANCE_UPLOAD_USAGE =
    vk::BufferUsageFlagBits::eTransferSrc;
constexpr vk::BufferUsageFlags DRAW_DATA_USAGE = GPU_DATA_USAGE;
constexpr vk::BufferUsageFlags MESH_TASK_COMMANDS_USAGE =
    GPU_DATA_USAGE | vk::BufferUsageFlagBits::eIndirectBuffer;
constexpr vk::BufferUsageFlags MESH_TASK_COMMAND_COUNT_USAGE =
    GPU_DATA_USAGE | vk::BufferUsageFlagBits::eIndirectBuffer;

/**
 * Instances one frame may draw. Every per-instance buffer below is sized from
 * it, costing 108 device bytes and 80 host bytes per instance, per
 * frame-in-flight slot. MeshDrawResources::init logs the resulting total.
 */
constexpr vk::DeviceSize MAX_MESH_INSTANCES = 28ull << 20;

constexpr vk::DeviceSize MESH_INSTANCES_CAPACITY =
    MAX_MESH_INSTANCES * sizeof(GPUMeshInstance);
constexpr vk::DeviceSize DRAW_DATA_CAPACITY =
    MAX_MESH_INSTANCES * sizeof(GPUDrawData);
constexpr vk::DeviceSize MESH_TASK_COMMANDS_CAPACITY =
    MAX_MESH_INSTANCES * sizeof(GPUMeshTaskCommand);
constexpr vk::DeviceSize MESH_TASK_COMMAND_COUNT_CAPACITY = 4ull << 10;

constexpr vk::DeviceSize DEVICE_BYTES_PER_SLOT =
    MESH_INSTANCES_CAPACITY + DRAW_DATA_CAPACITY +
    MESH_TASK_COMMANDS_CAPACITY + MESH_TASK_COMMAND_COUNT_CAPACITY;
constexpr vk::DeviceSize HOST_BYTES_PER_SLOT = MESH_INSTANCES_CAPACITY;

constexpr double GIB = 1024.0 * 1024.0 * 1024.0;

constexpr BufferAccess TRANSFER_WRITE{vk::PipelineStageFlagBits2::eAllTransfer,
                                      vk::AccessFlagBits2::eTransferWrite};
constexpr BufferAccess COMPUTE_READ{vk::PipelineStageFlagBits2::eComputeShader,
                                    vk::AccessFlagBits2::eShaderStorageRead};
constexpr BufferAccess COMPUTE_WRITE{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead |
        vk::AccessFlagBits2::eShaderStorageWrite};
constexpr BufferAccess MESH_SHADER_READ{
    vk::PipelineStageFlagBits2::eMeshShaderEXT,
    vk::AccessFlagBits2::eShaderStorageRead};
constexpr BufferAccess INDIRECT_READ{vk::PipelineStageFlagBits2::eDrawIndirect,
                                     vk::AccessFlagBits2::eIndirectCommandRead};

/// Keeps whichever barriers a group of AllocatedBuffer::use() calls produced.
void keep(std::vector<vk::BufferMemoryBarrier2>& barriers,
          std::optional<vk::BufferMemoryBarrier2> barrier) {
    if (barrier) barriers.push_back(*barrier);
}

} // namespace

bool detail::MeshDrawResourceSlot::init(VulkanContext& ctx) {
    if (!instances.init(ctx, MESH_INSTANCES_CAPACITY, MESH_INSTANCES_USAGE,
                        "mesh instances") ||
        !instanceUpload.init(ctx, MESH_INSTANCES_CAPACITY,
                             MESH_INSTANCE_UPLOAD_USAGE, "mesh instance upload") ||
        !drawData.init(ctx, DRAW_DATA_CAPACITY, DRAW_DATA_USAGE, "draw data") ||
        !commands.init(ctx, MESH_TASK_COMMANDS_CAPACITY, MESH_TASK_COMMANDS_USAGE,
                       "mesh task commands") ||
        !count.init(ctx, MESH_TASK_COMMAND_COUNT_CAPACITY,
                    MESH_TASK_COMMAND_COUNT_USAGE, "mesh task command count")) {
        destroy();
        return false;
    }
    return true;
}

void detail::MeshDrawResourceSlot::destroy() {
    count.destroy();
    commands.destroy();
    drawData.destroy();
    instanceUpload.destroy();
    instances.destroy();
}

detail::MeshDrawSpans detail::MeshDrawResourceSlot::spans(
    uint32_t instanceCount) const {
    const MeshDrawSpans result{instances.span<GPUMeshInstance>(instanceCount),
                               instanceUpload.span<GPUMeshInstance>(instanceCount),
                               drawData.span<GPUDrawData>(instanceCount),
                               commands.span<GPUMeshTaskCommand>(instanceCount),
                               count.span<uint32_t>(1)};
    if (!result) {
        logError(std::format(
            "MeshDrawResourceSlot::spans: {} instances exceeds the {} the "
            "mesh-draw buffers hold",
            instanceCount, MAX_MESH_INSTANCES));
        return {};
    }
    return result;
}

bool MeshDrawResources::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                             const std::filesystem::path& shaderPath) {
    for (auto& slot : slots_) {
        if (!slot.init(ctx)) {
            destroy();
            return false;
        }
    }
    if (!buildDrawCommands_.init(ctx.device(), shaderLoader, shaderPath)) {
        destroy();
        return false;
    }

    logMessage(std::format(
        "MeshDrawResources: {} instances per frame, {:.2f} GiB device and "
        "{:.2f} GiB host across {} frame-in-flight slots",
        MAX_MESH_INSTANCES,
        static_cast<double>(DEVICE_BYTES_PER_SLOT) * FRAMES_IN_FLIGHT / GIB,
        static_cast<double>(HOST_BYTES_PER_SLOT) * FRAMES_IN_FLIGHT / GIB,
        FRAMES_IN_FLIGHT));

    return true;
}

void MeshDrawResources::destroy() {
    buildDrawCommands_.destroy();
    for (auto& slot : slots_) slot.destroy();
}

void MeshDrawResources::copyPendingToUploadBuffer(
    std::vector<uint32_t>& indicesPendingUpload,
    std::span<const GPUMeshInstance> instances,
    const MappedSpan<GPUMeshInstance>& upload) {
    constexpr vk::DeviceSize STRIDE = sizeof(GPUMeshInstance);

    copyRegions_.clear();
    const uint32_t instanceCount = upload.gpu.count;
    ///Sort and dedup instance indices pending to upload
    std::ranges::sort(indicesPendingUpload);
    indicesPendingUpload.erase(
        std::ranges::unique(indicesPendingUpload).begin(), indicesPendingUpload.end());
    
    /// An index at or past instanceCount names an entry already removed.
    const std::ranges::subrange existingInstances{
        indicesPendingUpload.begin(),
        std::ranges::lower_bound(indicesPendingUpload, instanceCount)};

    /* We need to group instances so they are coppied in lowest possible amount of chunks. */
    constexpr auto isConsecutive = [](uint32_t a, uint32_t b) { return b == a + 1; };

    //Split consecutive instances into groups
    for (const auto group : existingInstances | std::views::chunk_by(isConsecutive)) {
        const uint32_t    first = group.front();
        const std::size_t count = group.size();

        std::ranges::copy(instances.subspan(first, count), upload.host + first);

        const vk::DeviceSize offset = vk::DeviceSize{first} * STRIDE;
        copyRegions_.push_back(
            vk::BufferCopy{offset, offset, vk::DeviceSize{count} * STRIDE});
    }

    indicesPendingUpload.clear();
}

PreparedMeshDraw MeshDrawResources::prepare(
    Frame::Recording& recording, std::span<const GPUMeshInstance> instances,
    std::span<const uint32_t> changed, const glm::mat4& viewProjection) {
    /* Every slot has its own device buffer, so each one receives the change. */
    for (auto& target : slots_)
        target.indicesPendingUpload.insert(target.indicesPendingUpload.end(),
                                           changed.begin(), changed.end());

    if (instances.empty() || instances.size() > std::numeric_limits<uint32_t>::max()) return {};

    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    detail::MeshDrawResourceSlot& slot = recording.select(slots_);

    const detail::MeshDrawSpans spans = slot.spans(instanceCount);
    if (!spans) return {};

    copyPendingToUploadBuffer(slot.indicesPendingUpload, instances, spans.upload);

    const vk::CommandBuffer commandBuffer = recording.commandBuffer();
    barriers_.clear();

    if (!copyRegions_.empty()) keep(barriers_, slot.instances.use(TRANSFER_WRITE));
    keep(barriers_, slot.count.use(TRANSFER_WRITE));
    recordBarriers(commandBuffer, barriers_);
    barriers_.clear();

    slot.instances.upload(commandBuffer, spans.upload.region.buffer, copyRegions_);
    zero(commandBuffer, spans.count.region);

    keep(barriers_, slot.instances.use(COMPUTE_READ));
    keep(barriers_, slot.drawData.use(COMPUTE_WRITE));
    keep(barriers_, slot.commands.use(COMPUTE_WRITE));
    keep(barriers_, slot.count.use(COMPUTE_WRITE));
    recordBarriers(commandBuffer, barriers_);
    barriers_.clear();

    BuildDrawCommandsPush push{};
    push.viewProj = viewProjection;
    push.instances = spans.instances.gpu.data;
    push.drawData = spans.drawData.gpu.data;
    push.commands = spans.commands.gpu.data;
    push.commandCount = spans.count.gpu.data;
    push.instanceCount = instanceCount;
    buildDrawCommands_.record(commandBuffer, push);

    keep(barriers_, slot.instances.use(MESH_SHADER_READ));
    keep(barriers_, slot.drawData.use(MESH_SHADER_READ));
    keep(barriers_, slot.commands.use(INDIRECT_READ));
    keep(barriers_, slot.count.use(INDIRECT_READ));
    recordBarriers(commandBuffer, barriers_);

    return PreparedMeshDraw{spans.instances.gpu.data, spans.drawData.gpu.data,
                            spans.commands.region, spans.count.region, instanceCount};
}
