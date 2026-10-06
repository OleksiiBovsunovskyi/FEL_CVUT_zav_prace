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
#include <string>
#include <utility>
#include <vector>

module MeshDrawResources;

import vulkan;
import vk_mem_alloc;
import Logger;
import VkUtil;

namespace {

constexpr vk::BufferUsageFlags MESH_INSTANCES_USAGE = GPU_DATA_USAGE;
/// Read as storage by scatter_instances.slang through a device address.
constexpr vk::BufferUsageFlags MESH_INSTANCE_UPDATE_USAGE =
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eShaderDeviceAddress;
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

constexpr vk::BufferUsageFlags VISIBILITY_USAGE = GPU_DATA_USAGE;

/// Device bytes one instance costs across instances, drawData, commands and visibility.
constexpr vk::DeviceSize DEVICE_BYTES_PER_INSTANCE =
    sizeof(GPUMeshInstance) + sizeof(GPUDrawData) + sizeof(GPUMeshTaskCommand) +
    sizeof(GPUInstanceVisibility);
/// Host bytes one instance costs in instanceUpdates.
constexpr vk::DeviceSize HOST_BYTES_PER_INSTANCE =
    sizeof(GPUMeshInstanceUpdate) + sizeof(GPUMeshletBase);

/// Meshlets a slot's meshletVisibility holds at minimum; every growth doubles from here.
constexpr uint32_t MIN_MESHLET_CAPACITY = 1u << 12;

/// Read as storage by mesh.slang through a device address.
constexpr vk::BufferUsageFlags MESHLET_BASES_USAGE = MESH_INSTANCE_UPDATE_USAGE;
constexpr vk::BufferUsageFlags MESHLET_VISIBILITY_USAGE = GPU_DATA_USAGE;

/// @return the capacity holding count, at least `minimum`, or 0 when count is past MAX_INSTANCE_CAPACITY.
uint32_t capacityFor(uint32_t count, uint32_t minimum) {
    if (count > MAX_INSTANCE_CAPACITY) return 0;
    return std::max(minimum, std::bit_ceil(count));
}

constexpr double MIB = 1024.0 * 1024.0;

constexpr BufferAccess TRANSFER_WRITE{vk::PipelineStageFlagBits2::eAllTransfer,
                                      vk::AccessFlagBits2::eTransferWrite};
constexpr BufferAccess COMPUTE_SCATTER_WRITE{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite};
constexpr BufferAccess COMPUTE_READ{vk::PipelineStageFlagBits2::eComputeShader,
                                    vk::AccessFlagBits2::eShaderStorageRead};
constexpr BufferAccess COMPUTE_WRITE{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead |
        vk::AccessFlagBits2::eShaderStorageWrite};
constexpr BufferAccess MESH_SHADER_READ{
    vk::PipelineStageFlagBits2::eMeshShaderEXT,
    vk::AccessFlagBits2::eShaderStorageRead};
constexpr BufferAccess MESH_SHADER_WRITE{
    vk::PipelineStageFlagBits2::eMeshShaderEXT,
    vk::AccessFlagBits2::eShaderStorageRead |
        vk::AccessFlagBits2::eShaderStorageWrite};
constexpr BufferAccess INDIRECT_READ{vk::PipelineStageFlagBits2::eDrawIndirect,
                                     vk::AccessFlagBits2::eIndirectCommandRead};

/// Keeps whichever barriers a group of AllocatedBuffer::use() calls produced.
void keep(std::vector<vk::BufferMemoryBarrier2>& barriers,
          std::optional<vk::BufferMemoryBarrier2> barrier) {
    if (barrier) barriers.push_back(*barrier);
}

} // namespace

bool detail::MeshDrawResourceSlot::reserve(VulkanContext& ctx,
                                          uint32_t instanceCount,
                                          std::vector<RetiredBuffer>& retired) {
    if (instanceCount <= instanceCapacity) return true;

    const uint32_t capacity = capacityFor(instanceCount, MIN_INSTANCE_CAPACITY);
    if (capacity == 0) {
        logError(std::format(
            "MeshDrawResourceSlot: {} instances is past the {} a slot can hold",
            instanceCount, MAX_INSTANCE_CAPACITY));
        return false;
    }

    /* The next recording reads this buffer on the GPU after this call returns. */
    if (visibility) retired.push_back(RetiredBuffer{std::move(visibility), FRAMES_IN_FLIGHT});
    if (meshletVisibility)
        retired.push_back(RetiredBuffer{std::move(meshletVisibility), FRAMES_IN_FLIGHT});
    destroy();

    const vk::DeviceSize instanceBytes =
        vk::DeviceSize{capacity} * sizeof(GPUMeshInstance);
    if (!instances.init(ctx, instanceBytes, MESH_INSTANCES_USAGE,
                        "mesh instances") ||
        !instanceUpdates.init(ctx,
                              vk::DeviceSize{capacity} * sizeof(GPUMeshInstanceUpdate),
                              MESH_INSTANCE_UPDATE_USAGE, "mesh instance updates") ||
        !drawData.init(ctx, vk::DeviceSize{capacity} * sizeof(GPUDrawData),
                       DRAW_DATA_USAGE, "draw data") ||
        !commands.init(ctx, vk::DeviceSize{capacity} * sizeof(GPUMeshTaskCommand),
                       MESH_TASK_COMMANDS_USAGE, "mesh task commands") ||
        !count.init(ctx, MESH_TASK_COMMAND_COUNT_CAPACITY,
                    MESH_TASK_COMMAND_COUNT_USAGE, "mesh task command count") ||
        !visibility.init(ctx, vk::DeviceSize{capacity} * sizeof(GPUInstanceVisibility),
                         VISIBILITY_USAGE, "instance visibility") ||
        !meshletBases.init(ctx, vk::DeviceSize{capacity} * sizeof(GPUMeshletBase),
                           MESHLET_BASES_USAGE, "meshlet bases")) {
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

bool detail::MeshDrawResourceSlot::reserveMeshlets(VulkanContext& ctx,
                                                  uint32_t meshletCount,
                                                  std::vector<RetiredBuffer>& retired) {
    if (meshletCount <= meshletCapacity) return true;

    const uint32_t capacity = capacityFor(meshletCount, MIN_MESHLET_CAPACITY);
    if (capacity == 0) {
        logError(std::format(
            "MeshDrawResourceSlot: {} meshlets is past the {} a slot can hold",
            meshletCount, MAX_INSTANCE_CAPACITY));
        return false;
    }

    /* The next recording reads this buffer on the GPU after this call returns. */
    if (meshletVisibility)
        retired.push_back(RetiredBuffer{std::move(meshletVisibility), FRAMES_IN_FLIGHT});
    meshletVisibility.destroy();
    meshletVisibilityCount = 0;
    meshletCapacity        = 0;

    if (!meshletVisibility.init(ctx, vk::DeviceSize{capacity} * sizeof(GPUMeshletVisibility),
                                MESHLET_VISIBILITY_USAGE, "meshlet visibility"))
        return false;
    meshletCapacity = capacity;
    return true;
}

void detail::MeshDrawResourceSlot::destroy() {
    instanceCapacity = 0;
    visibilityCount  = 0;
    meshletCapacity  = 0;
    meshletVisibilityCount = 0;
    indicesPendingUpload.clear();
    meshletVisibility.destroy();
    meshletBases.destroy();
    visibility.destroy();
    count.destroy();
    commands.destroy();
    drawData.destroy();
    instanceUpdates.destroy();
    instances.destroy();
}

detail::MeshDrawSpans detail::MeshDrawResourceSlot::spans(
    uint32_t instanceCount) const {
    const MeshDrawSpans result{instances.span<GPUMeshInstance>(instanceCount),
                               drawData.span<GPUDrawData>(instanceCount),
                               commands.span<GPUMeshTaskCommand>(instanceCount),
                               count.span<GPUDrawCommandCount>(1)};
    if (!result) {
        logError(std::format(
            "MeshDrawResourceSlot::spans: {} instances exceeds the {} the "
            "mesh-draw buffers hold",
            instanceCount, instanceCapacity));
        return {};
    }
    return result;
}

bool MeshDrawResources::init(
    VulkanContext& ctx, ShaderLoader& shaderLoader,
    const std::filesystem::path& scatterShaderPath,
    const std::filesystem::path& buildDrawCommandsShaderPath) {
    ctx_ = &ctx;
    if (!scatterInstances_.init(ctx.device(), shaderLoader, scatterShaderPath) ||
        !buildDrawCommands_.init(ctx.device(), shaderLoader,
                                 buildDrawCommandsShaderPath)) {
        destroy();
        return false;
    }
    return true;
}

void MeshDrawResources::destroy() {
    buildDrawCommands_.destroy();
    scatterInstances_.destroy();
    retired_.clear();
    for (auto& slot : slots_) slot.destroy();
    ctx_ = nullptr;
}

MappedSpan<GPUMeshInstanceUpdate> detail::MeshDrawResourceSlot::packPendingUpdates(
    std::span<const GPUMeshInstance> instances) {
    const auto instanceCount = static_cast<uint32_t>(instances.size());
    const auto pendingCount  = static_cast<uint32_t>(indicesPendingUpload.size());

    const MappedSpan<GPUMeshInstanceUpdate> updates =
        instanceUpdates.span<GPUMeshInstanceUpdate>(
            std::min(pendingCount, instanceCount));

    uint32_t written = 0;
    if (pendingCount >= instanceCount) {
        /* Repeats make the pending list longer than the update buffer holds.*/
        for (uint32_t index = 0; index < instanceCount; ++index)
            updates.host[written++] =
                GPUMeshInstanceUpdate{index, {}, instances[index]};
    } else {
        for (const uint32_t index : indicesPendingUpload) {
            if (index >= instanceCount) continue;
            updates.host[written++] =
                GPUMeshInstanceUpdate{index, {}, instances[index]};
        }
    }
    indicesPendingUpload.clear();

    return instanceUpdates.span<GPUMeshInstanceUpdate>(written);
}

PreparedMeshDraw MeshDrawResources::prepare(
    Frame::Recording& recording, std::span<const GPUMeshInstance> instances,
    std::span<const uint32_t> changed,
    std::span<const uint32_t> meshletCounts, GpuPtr<GPUFrameData> frame,
    GpuPassTimings& timings) {
    /* Every slot has its own device buffer, so each one receives the change. */
    for (auto& target : slots_)
        target.indicesPendingUpload.insert(target.indicesPendingUpload.end(),
                                           changed.begin(), changed.end());

    for (detail::RetiredBuffer& retired : retired_) --retired.recordingsLeft;
    std::erase_if(retired_, [](const detail::RetiredBuffer& retired) {
        return retired.recordingsLeft == 0;
    });

    detail::MeshDrawResourceSlot& slot = recording.select(slots_);
    slot.visibilityCount = 0;
    slot.meshletVisibilityCount = 0;
    if (instances.empty() || instances.size() > std::numeric_limits<uint32_t>::max()) return {};
    if (meshletCounts.size() != instances.size()) {
        logError(std::format("MeshDrawResources::prepare: {} meshlet counts for {} instances",
                             meshletCounts.size(), instances.size()));
        return {};
    }

    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    if (!ctx_ || !slot.reserve(*ctx_, instanceCount, retired_)) return {};

    const detail::MeshDrawSpans spans = slot.spans(instanceCount);
    if (!spans) return {};

    const MappedSpan<GPUMeshletBase> bases = slot.meshletBases.span<GPUMeshletBase>(instanceCount);
    if (!bases) return {};

    uint64_t meshletTotal = 0;
    for (uint32_t index = 0; index < instanceCount; ++index) {
        bases.host[index].first = static_cast<uint32_t>(meshletTotal);
        meshletTotal += meshletCounts[index];
    }
    if (meshletTotal > MAX_INSTANCE_CAPACITY) {
        logError(std::format("MeshDrawResources::prepare: {} meshlets is past the {} a slot can hold",
                             meshletTotal, MAX_INSTANCE_CAPACITY));
        return {};
    }
    const auto meshletCount = static_cast<uint32_t>(meshletTotal);
    if (!slot.reserveMeshlets(*ctx_, meshletCount, retired_)) return {};

    const MappedSpan<GPUMeshInstanceUpdate> updates =
        slot.packPendingUpdates(instances);

    const vk::CommandBuffer commandBuffer = recording.commandBuffer();
    barriers_.clear();

    if (updates) keep(barriers_, slot.instances.use(COMPUTE_SCATTER_WRITE));
    keep(barriers_, slot.count.use(TRANSFER_WRITE));
    recordBarriers(commandBuffer, barriers_);
    barriers_.clear();

    ScatterInstancesPush scatter{};
    scatter.updates     = updates.gpu.data;
    scatter.instances   = spans.instances.gpu.data;
    scatter.updateCount = updates.gpu.count;
    scatterInstances_.record(commandBuffer, scatter, updates.gpu.count);

    zero(commandBuffer, spans.count.region);

    timings.mark(commandBuffer, "scatter_instances");

    detail::MeshDrawResourceSlot& previous = recording.frameInFlight().previous().select(slots_);

    keep(barriers_, slot.instances.use(COMPUTE_READ));
    keep(barriers_, slot.drawData.use(COMPUTE_WRITE));
    keep(barriers_, slot.commands.use(COMPUTE_WRITE));
    keep(barriers_, slot.count.use(COMPUTE_WRITE));
    if (previous.visibilityCount != 0) keep(barriers_, previous.visibility.use(COMPUTE_READ));
    recordBarriers(commandBuffer, barriers_);
    barriers_.clear();

    BuildDrawCommandsPush push{};
    push.frame = frame;
    push.instances = spans.instances.gpu.data;
    push.drawData = spans.drawData.gpu.data;
    push.commands = spans.commands.gpu.data;
    push.commandCount = spans.count.gpu.data;
    push.previousVisibility = previous.visibility.gpuAddress<GPUInstanceVisibility>();
    push.visibility = slot.visibility.gpuAddress<GPUInstanceVisibility>();
    push.previousVisibilityCount = previous.visibilityCount;
    push.instanceCount = instanceCount;
    push.pass = BUILD_PASS_FIRST;
    buildDrawCommands_.record(commandBuffer, push, instanceCount);

    keep(barriers_, slot.instances.use(MESH_SHADER_READ));
    keep(barriers_, slot.drawData.use(MESH_SHADER_READ));
    keep(barriers_, slot.commands.use(INDIRECT_READ));
    keep(barriers_, slot.count.use(INDIRECT_READ));
    if (previous.meshletVisibilityCount != 0)
        keep(barriers_, previous.meshletVisibility.use(MESH_SHADER_READ));
    recordBarriers(commandBuffer, barriers_);
    timings.mark(commandBuffer, "build_draw_commands");

    PreparedMeshDraw prepared{spans.instances.gpu.data, spans.drawData.gpu.data,
                              spans.commands.region, spans.count.region, instanceCount};
    prepared.meshletBase = bases.gpu.data;
    prepared.previousMeshletVisibility =
        previous.meshletVisibility.gpuAddress<GPUMeshletVisibility>();
    prepared.meshletVisibility = slot.meshletVisibility.gpuAddress<GPUMeshletVisibility>();
    prepared.previousMeshletVisibilityCount = previous.meshletVisibilityCount;
    prepared.meshletCount = meshletCount;
    return prepared;
}

PreparedMeshDraw MeshDrawResources::cullSecondPass(Frame::Recording& recording,
                                                   const PreparedMeshDraw& prepared,
                                                   GpuPtr<GPUFrameData> frame,
                                                   const DepthPyramidView& hiZ,
                                                   GpuPassTimings& timings) {
    if (!prepared) return {};

    detail::MeshDrawResourceSlot& slot = recording.select(slots_);
    detail::MeshDrawResourceSlot& previous = recording.frameInFlight().previous().select(slots_);
    const detail::MeshDrawSpans spans = slot.spans(prepared.instanceCount);
    if (!spans) return {};

    const vk::CommandBuffer commandBuffer = recording.commandBuffer();
    barriers_.clear();

    keep(barriers_, slot.count.use(TRANSFER_WRITE));
    recordBarriers(commandBuffer, barriers_);
    barriers_.clear();

    zero(commandBuffer, spans.count.region);

    keep(barriers_, slot.instances.use(COMPUTE_READ));
    keep(barriers_, slot.drawData.use(COMPUTE_WRITE));
    keep(barriers_, slot.commands.use(COMPUTE_WRITE));
    keep(barriers_, slot.count.use(COMPUTE_WRITE));
    keep(barriers_, slot.visibility.use(COMPUTE_WRITE));
    if (previous.visibilityCount != 0) keep(barriers_, previous.visibility.use(COMPUTE_READ));
    recordBarriers(commandBuffer, barriers_);
    barriers_.clear();

    BuildDrawCommandsPush push{};
    push.frame = frame;
    push.instances = spans.instances.gpu.data;
    push.drawData = spans.drawData.gpu.data;
    push.commands = spans.commands.gpu.data;
    push.commandCount = spans.count.gpu.data;
    push.previousVisibility = previous.visibility.gpuAddress<GPUInstanceVisibility>();
    push.visibility = slot.visibility.gpuAddress<GPUInstanceVisibility>();
    push.hiZ = hiZ.data;
    push.previousVisibilityCount = previous.visibilityCount;
    push.instanceCount = prepared.instanceCount;
    push.pass = BUILD_PASS_SECOND;
    push.hiZWidth = hiZ.extent.width;
    push.hiZHeight = hiZ.extent.height;
    push.hiZLevels = hiZ ? hiZ.levels : 0;
    buildDrawCommands_.record(commandBuffer, push, prepared.instanceCount);

    keep(barriers_, slot.instances.use(MESH_SHADER_READ));
    keep(barriers_, slot.drawData.use(MESH_SHADER_READ));
    keep(barriers_, slot.commands.use(INDIRECT_READ));
    keep(barriers_, slot.count.use(INDIRECT_READ));
    keep(barriers_, slot.meshletVisibility.use(MESH_SHADER_WRITE));
    recordBarriers(commandBuffer, barriers_);
    barriers_.clear();

    slot.visibilityCount = prepared.instanceCount;
    slot.meshletVisibilityCount = prepared.meshletCount;
    timings.mark(commandBuffer, "cull second pass");

    PreparedMeshDraw second = prepared;
    second.pass = BUILD_PASS_SECOND;
    second.hiZ  = hiZ;
    return second;
}
