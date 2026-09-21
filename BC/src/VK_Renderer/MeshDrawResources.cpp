module;

#include "glm/glm.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <numeric>
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

/// Instances a slot holds at minimum; every growth doubles from here.
constexpr uint32_t MIN_INSTANCE_CAPACITY = 1u << 10;

/// Largest capacity std::bit_ceil can express in a uint32_t.
constexpr uint32_t MAX_INSTANCE_CAPACITY = 1u << 31;

constexpr vk::DeviceSize MESH_TASK_COMMAND_COUNT_CAPACITY = 4ull << 10;

/// Device bytes one instance costs across instances, drawData and commands.
constexpr vk::DeviceSize DEVICE_BYTES_PER_INSTANCE =
    sizeof(GPUMeshInstance) + sizeof(GPUDrawData) + sizeof(GPUMeshTaskCommand);
/// Host bytes one instance costs in instanceUpload.
constexpr vk::DeviceSize HOST_BYTES_PER_INSTANCE = sizeof(GPUMeshInstance);

/// @return the capacity holding count, or 0 when count is past MAX_INSTANCE_CAPACITY.
uint32_t capacityFor(uint32_t count) {
    if (count > MAX_INSTANCE_CAPACITY) return 0;
    return std::max(MIN_INSTANCE_CAPACITY, std::bit_ceil(count));
}

constexpr double MIB = 1024.0 * 1024.0;

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

bool detail::MeshDrawResourceSlot::reserve(VulkanContext& ctx,
                                          uint32_t instanceCount) {
    if (instanceCount <= instanceCapacity) return true;

    const uint32_t capacity = capacityFor(instanceCount);
    if (capacity == 0) {
        logError(std::format(
            "MeshDrawResourceSlot: {} instances is past the {} a slot can hold",
            instanceCount, MAX_INSTANCE_CAPACITY));
        return false;
    }

    destroy();

    const vk::DeviceSize instanceBytes =
        vk::DeviceSize{capacity} * sizeof(GPUMeshInstance);
    if (!instances.init(ctx, instanceBytes, MESH_INSTANCES_USAGE,
                        "mesh instances") ||
        !instanceUpload.init(ctx, instanceBytes, MESH_INSTANCE_UPLOAD_USAGE,
                             "mesh instance upload") ||
        !drawData.init(ctx, vk::DeviceSize{capacity} * sizeof(GPUDrawData),
                       DRAW_DATA_USAGE, "draw data") ||
        !commands.init(ctx, vk::DeviceSize{capacity} * sizeof(GPUMeshTaskCommand),
                       MESH_TASK_COMMANDS_USAGE, "mesh task commands") ||
        !count.init(ctx, MESH_TASK_COMMAND_COUNT_CAPACITY,
                    MESH_TASK_COMMAND_COUNT_USAGE, "mesh task command count")) {
        destroy();
        return false;
    }
    instanceCapacity = capacity;

    /* The new device buffer holds nothing, so every live instance is written
     * again from the caller's array. */
    indicesPendingUpload.resize(instanceCount);
    std::iota(indicesPendingUpload.begin(), indicesPendingUpload.end(), 0u);

    logMessage(std::format(
        "MeshDrawResourceSlot: {} instances, {:.2f} MiB device and {:.2f} MiB host",
        capacity,
        static_cast<double>(vk::DeviceSize{capacity} * DEVICE_BYTES_PER_INSTANCE +
                            MESH_TASK_COMMAND_COUNT_CAPACITY) / MIB,
        static_cast<double>(vk::DeviceSize{capacity} * HOST_BYTES_PER_INSTANCE) / MIB));
    return true;
}

void detail::MeshDrawResourceSlot::destroy() {
    instanceCapacity = 0;
    indicesPendingUpload.clear();
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
            instanceCount, instanceCapacity));
        return {};
    }
    return result;
}

bool MeshDrawResources::init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                             const std::filesystem::path& shaderPath) {
    ctx_ = &ctx;
    if (!buildDrawCommands_.init(ctx.device(), shaderLoader, shaderPath)) {
        destroy();
        return false;
    }
    return true;
}

void MeshDrawResources::destroy() {
    buildDrawCommands_.destroy();
    for (auto& slot : slots_) slot.destroy();
    ctx_ = nullptr;
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
    std::span<const uint32_t> changed, GpuPtr<GPUCameraData> camera) {
    /* Every slot has its own device buffer, so each one receives the change. */
    for (auto& target : slots_)
        target.indicesPendingUpload.insert(target.indicesPendingUpload.end(),
                                           changed.begin(), changed.end());

    if (instances.empty() || instances.size() > std::numeric_limits<uint32_t>::max()) return {};

    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    detail::MeshDrawResourceSlot& slot = recording.select(slots_);
    if (!ctx_ || !slot.reserve(*ctx_, instanceCount)) return {};

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
    push.camera = camera;
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
