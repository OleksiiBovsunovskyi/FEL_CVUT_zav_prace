module;

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>
#include "glm/glm.hpp"

#include <cstddef>
#include <cstring>
#include <limits>
#include <filesystem>
#include <string>

module MeshDrawResources;

import Logger;
import VkUtil;

namespace {

constexpr vk::BufferUsageFlags MESH_INSTANCES_USAGE = GPU_DATA_USAGE;
constexpr vk::BufferUsageFlags DRAW_DATA_USAGE = GPU_DATA_USAGE;
constexpr vk::BufferUsageFlags MESH_TASK_COMMANDS_USAGE =
    GPU_DATA_USAGE | vk::BufferUsageFlagBits::eIndirectBuffer;
constexpr vk::BufferUsageFlags MESH_TASK_COMMAND_COUNT_USAGE =
    GPU_DATA_USAGE | vk::BufferUsageFlagBits::eIndirectBuffer;

constexpr vk::DeviceSize MESH_INSTANCES_CAPACITY = 8ull << 20;
constexpr vk::DeviceSize DRAW_DATA_CAPACITY = 2ull << 20;
constexpr vk::DeviceSize MESH_TASK_COMMANDS_CAPACITY = 2ull << 20;
constexpr vk::DeviceSize MESH_TASK_COMMAND_COUNT_CAPACITY = 4ull << 10;

constexpr vma::AllocationCreateFlags MAPPED_FLAGS =
    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
    vma::AllocationCreateFlagBits::eMapped;

[[nodiscard]] bool fits(vk::DeviceSize capacity, uint32_t count, size_t stride) {
    return vk::DeviceSize{count} * stride <= capacity;
}

} // namespace

bool detail::AllocatedBuffer::init(
    VulkanContext& ctx, vk::DeviceSize capacity, vk::BufferUsageFlags usage,
    vma::MemoryUsage memory, vma::AllocationCreateFlags flags, bool warnIfHost,
    const char* name) {
    if (buffer_) {
        logError(std::string("MeshDrawResources: init called twice for ") + name);
        return false;
    }
    if (capacity == 0) {
        logError(std::string("MeshDrawResources: zero capacity for ") + name);
        return false;
    }

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size        = capacity;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    vma::AllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memory;
    allocationInfo.flags = flags;

    vma::AllocationInfo resultInfo{};
    allocator_ = ctx.allocator();
    const vk::Result result = allocator_.createBuffer(
        &bufferInfo, &allocationInfo, &buffer_, &allocation_, &resultInfo);
    if (result != vk::Result::eSuccess) {
        logError(std::string("MeshDrawResources: vmaCreateBuffer failed for ") +
                 name + ": VkResult " + vk::to_string(result));
        destroy();
        return false;
    }

    capacity_ = capacity;
    mapped_ = static_cast<std::byte*>(resultInfo.pMappedData);
    allocator_.setAllocationName(allocation_, name);

    if (warnIfHost) {
        const vk::MemoryPropertyFlags properties =
            allocator_.getAllocationMemoryProperties(allocation_);
        if (!(properties & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
            logError(std::string("MeshDrawResources: ") + name +
                     " requested device-local mapped memory and received host memory");
        }
    }

    if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        vk::BufferDeviceAddressInfo addressInfo{};
        addressInfo.buffer = buffer_;
        deviceAddress_ = GpuPtr<std::byte>{ctx.device().getBufferAddress(addressInfo)};
        if (deviceAddress_.address == 0) {
            logError(std::string("MeshDrawResources: no device address for ") + name);
            destroy();
            return false;
        }
    }
    return true;
}

void detail::AllocatedBuffer::destroy() {
    if (buffer_) allocator_.destroyBuffer(buffer_, allocation_);
    allocator_ = nullptr;
    buffer_ = nullptr;
    allocation_ = nullptr;
    capacity_ = 0;
    deviceAddress_ = {};
    mapped_ = nullptr;
}

BufferRegion detail::AllocatedBuffer::region(vk::DeviceSize size) const {
    if (!buffer_ || size == 0 || size > capacity_) return {};
    return BufferRegion{buffer_, 0, size};
}

bool detail::MeshInstancesBuffer::init(VulkanContext& ctx) {
    if (!buffer_.init(ctx, MESH_INSTANCES_CAPACITY, MESH_INSTANCES_USAGE,
                      vma::MemoryUsage::eAuto, MAPPED_FLAGS, true,
                      "mesh instances"))
        return false;

    mapped_ = reinterpret_cast<GPUMeshInstance*>(buffer_.mapped());
    if (!mapped_) {
        logError("MeshDrawResources: mesh instances allocation is not mapped");
        destroy();
        return false;
    }
    return true;
}

void detail::MeshInstancesBuffer::destroy() {
    mapped_ = nullptr;
    buffer_.destroy();
}

MappedSpan<GPUMeshInstance> detail::MeshInstancesBuffer::span(uint32_t instanceCount) const {
    if (!fits(buffer_.capacity(), instanceCount, sizeof(GPUMeshInstance))) return {};
    return MappedSpan<GPUMeshInstance>{
        buffer_.region(vk::DeviceSize{instanceCount} * sizeof(GPUMeshInstance)),
        GpuSpan<GPUMeshInstance>{buffer_.gpuAddress<GPUMeshInstance>(), instanceCount},
        mapped_};
}

bool detail::DrawDataBuffer::init(VulkanContext& ctx) {
    return buffer_.init(ctx, DRAW_DATA_CAPACITY, DRAW_DATA_USAGE,
                        vma::MemoryUsage::eAutoPreferDevice, {}, false, "draw data");
}

void detail::DrawDataBuffer::destroy() {
    buffer_.destroy();
}

DeviceSpan<GPUDrawData> detail::DrawDataBuffer::span(uint32_t instanceCount) const {
    if (!fits(buffer_.capacity(), instanceCount, sizeof(GPUDrawData))) return {};
    return DeviceSpan<GPUDrawData>{
        buffer_.region(vk::DeviceSize{instanceCount} * sizeof(GPUDrawData)),
        GpuSpan<GPUDrawData>{buffer_.gpuAddress<GPUDrawData>(), instanceCount}};
}

bool detail::MeshTaskCommandsBuffer::init(VulkanContext& ctx) {
    return buffer_.init(ctx, MESH_TASK_COMMANDS_CAPACITY, MESH_TASK_COMMANDS_USAGE,
                        vma::MemoryUsage::eAutoPreferDevice, {}, false,
                        "mesh task commands");
}

void detail::MeshTaskCommandsBuffer::destroy() {
    buffer_.destroy();
}

DeviceSpan<GPUMeshTaskCommand> detail::MeshTaskCommandsBuffer::span(
    uint32_t instanceCount) const {
    if (!fits(buffer_.capacity(), instanceCount, sizeof(GPUMeshTaskCommand))) return {};
    return DeviceSpan<GPUMeshTaskCommand>{
        buffer_.region(vk::DeviceSize{instanceCount} * sizeof(GPUMeshTaskCommand)),
        GpuSpan<GPUMeshTaskCommand>{buffer_.gpuAddress<GPUMeshTaskCommand>(), instanceCount}};
}

bool detail::MeshTaskCommandCountBuffer::init(VulkanContext& ctx) {
    return buffer_.init(ctx, MESH_TASK_COMMAND_COUNT_CAPACITY,
                        MESH_TASK_COMMAND_COUNT_USAGE,
                        vma::MemoryUsage::eAutoPreferDevice, {}, false,
                        "mesh task command count");
}

void detail::MeshTaskCommandCountBuffer::destroy() {
    buffer_.destroy();
}

DeviceSpan<uint32_t> detail::MeshTaskCommandCountBuffer::span() const {
    constexpr uint32_t count = 1;
    if (!fits(buffer_.capacity(), count, sizeof(uint32_t))) return {};
    return DeviceSpan<uint32_t>{
        buffer_.region(sizeof(uint32_t)),
        GpuSpan<uint32_t>{buffer_.gpuAddress<uint32_t>(), count}};
}

bool detail::MeshDrawResourceSlot::init(VulkanContext& ctx) {
    if (!instances.init(ctx) || !drawData.init(ctx) || !commands.init(ctx) || !count.init(ctx)) {
        destroy();
        return false;
    }
    return true;
}

void detail::MeshDrawResourceSlot::destroy() {
    count.destroy();
    commands.destroy();
    drawData.destroy();
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
    const MappedSpan<GPUMeshInstance> instanceSpan = slot.instances.span(instanceCount);
    const DeviceSpan<GPUDrawData> drawDataSpan = slot.drawData.span(instanceCount);
    const DeviceSpan<GPUMeshTaskCommand> commandSpan = slot.commands.span(instanceCount);
    const DeviceSpan<uint32_t> countSpan = slot.count.span();
    if (!instanceSpan || !drawDataSpan || !commandSpan || !countSpan) {
        logError("MeshDrawResources::prepare: mesh-draw buffer capacity exceeded");
        return {};
    }

    std::memcpy(instanceSpan.host, instances.data(),
                instances.size_bytes());

    const vk::CommandBuffer commandBuffer = recording.commandBuffer();
    zero(commandBuffer, countSpan.region);
    barrier(commandBuffer,
            vk::PipelineStageFlagBits2::eClear, vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead |
                vk::AccessFlagBits2::eShaderStorageWrite);

    BuildDrawCommandsPush push{};
    push.viewProj = viewProjection;
    push.instances = instanceSpan.gpu.data;
    push.drawData = drawDataSpan.gpu.data;
    push.commands = commandSpan.gpu.data;
    push.commandCount = countSpan.gpu.data;
    push.instanceCount = instanceCount;
    buildDrawCommands_.record(commandBuffer, push);

    barrier(commandBuffer,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eDrawIndirect |
                vk::PipelineStageFlagBits2::eMeshShaderEXT,
            vk::AccessFlagBits2::eIndirectCommandRead |
                vk::AccessFlagBits2::eShaderStorageRead);

    return PreparedMeshDraw{instanceSpan.gpu.data, drawDataSpan.gpu.data,
                            commandSpan.region, countSpan.region, instanceCount};
}
