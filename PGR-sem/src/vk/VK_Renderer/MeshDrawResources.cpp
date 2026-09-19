module;

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>
#include "glm/glm.hpp"

#include <cstddef>
#include <format>
#include <limits>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

module MeshDrawResources;

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

/// Instances one frame may draw. Every per-instance buffer below is sized from it.

constexpr vk::DeviceSize MAX_MESH_INSTANCES = 4ull << 20;

constexpr vk::DeviceSize MESH_INSTANCES_CAPACITY =
    MAX_MESH_INSTANCES * sizeof(GPUMeshInstance);
constexpr vk::DeviceSize DRAW_DATA_CAPACITY =
    MAX_MESH_INSTANCES * sizeof(GPUDrawData);
constexpr vk::DeviceSize MESH_TASK_COMMANDS_CAPACITY =
    MAX_MESH_INSTANCES * sizeof(GPUMeshTaskCommand);
constexpr vk::DeviceSize MESH_TASK_COMMAND_COUNT_CAPACITY = 4ull << 10;

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
    return true;
}

void MeshDrawResources::destroy() {
    buildDrawCommands_.destroy();
    for (auto& slot : slots_) slot.destroy();
}

PreparedMeshDraw MeshDrawResources::prepare(
    Frame::Recording& recording, std::span<const GPUMeshInstance> instances,
    const glm::mat4& viewProjection) {
    if (instances.empty() || instances.size() > std::numeric_limits<uint32_t>::max()) return {};

    const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    detail::MeshDrawResourceSlot& slot = recording.select(slots_);

    const DeviceSpan<GPUMeshInstance> instanceSpan =
        slot.instances.span<GPUMeshInstance>(instanceCount);
    const MappedSpan<GPUMeshInstance> uploadSpan =
        slot.instanceUpload.span<GPUMeshInstance>(instanceCount);
    const DeviceSpan<GPUDrawData> drawDataSpan =
        slot.drawData.span<GPUDrawData>(instanceCount);
    const DeviceSpan<GPUMeshTaskCommand> commandSpan =
        slot.commands.span<GPUMeshTaskCommand>(instanceCount);
    const DeviceSpan<uint32_t> countSpan = slot.count.span<uint32_t>(1);

    if (!instanceSpan || !uploadSpan || !drawDataSpan || !commandSpan || !countSpan) {
        logError(std::format(
            "MeshDrawResources::prepare: {} instances exceeds the {} the "
            "mesh-draw buffers hold",
            instanceCount, MAX_MESH_INSTANCES));
        return {};
    }

    if (!slot.instanceUpload.write(instances.data(), instanceCount)) {
        logError("MeshDrawResources::prepare: writing the instance upload buffer failed");
        return {};
    }

    const vk::CommandBuffer commandBuffer = recording.commandBuffer();
    std::vector<vk::BufferMemoryBarrier2> barriers;
    barriers.reserve(5);

    keep(barriers, slot.instances.use(TRANSFER_WRITE));
    keep(barriers, slot.count.use(TRANSFER_WRITE));
    recordBarriers(commandBuffer, barriers);
    barriers.clear();

    slot.instances.upload(commandBuffer, uploadSpan.region);
    zero(commandBuffer, countSpan.region);

    keep(barriers, slot.instances.use(COMPUTE_READ));
    keep(barriers, slot.drawData.use(COMPUTE_WRITE));
    keep(barriers, slot.commands.use(COMPUTE_WRITE));
    keep(barriers, slot.count.use(COMPUTE_WRITE));
    recordBarriers(commandBuffer, barriers);
    barriers.clear();

    BuildDrawCommandsPush push{};
    push.viewProj = viewProjection;
    push.instances = instanceSpan.gpu.data;
    push.drawData = drawDataSpan.gpu.data;
    push.commands = commandSpan.gpu.data;
    push.commandCount = countSpan.gpu.data;
    push.instanceCount = instanceCount;
    buildDrawCommands_.record(commandBuffer, push);

    keep(barriers, slot.instances.use(MESH_SHADER_READ));
    keep(barriers, slot.drawData.use(MESH_SHADER_READ));
    keep(barriers, slot.commands.use(INDIRECT_READ));
    keep(barriers, slot.count.use(INDIRECT_READ));
    recordBarriers(commandBuffer, barriers);

    return PreparedMeshDraw{instanceSpan.gpu.data, drawDataSpan.gpu.data,
                            commandSpan.region, countSpan.region, instanceCount};
}
