module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

module BufferManager;

import GPUTypes;
import Logger;

namespace {

constexpr size_t index(StaticBufferKind kind) {
    return static_cast<size_t>(kind);
}

/// Runtime form of the traits, for the loops that walk every buffer.
struct BufferSpec {
    const char*                name       = "";
    vk::BufferUsageFlags       usage      = {};
    vma::MemoryUsage           memory     = vma::MemoryUsage::eAuto;
    vma::AllocationCreateFlags flags      = {};
    bool                       warnIfHost = false;
};

/* Instantiating over the whole enum is what makes a missing traits row a
 * compile error instead of a buffer that is silently never created. */
template <template <auto> class Traits, typename Kind, size_t... Index>
constexpr auto specsOf(std::index_sequence<Index...>) {
    return std::array<BufferSpec, sizeof...(Index)>{
        BufferSpec{Traits<static_cast<Kind>(Index)>::name,
                   Traits<static_cast<Kind>(Index)>::usage,
                   Traits<static_cast<Kind>(Index)>::Buffer::memory,
                   Traits<static_cast<Kind>(Index)>::Buffer::flags,
                   Traits<static_cast<Kind>(Index)>::Buffer::warnIfHost}...};
}

constexpr auto STATIC_SPECS = specsOf<StaticBufferTraits, StaticBufferKind>(
    std::make_index_sequence<STATIC_BUFFER_COUNT>{});
constexpr BufferSpec UPLOAD_SPEC{
    "upload",
    vk::BufferUsageFlagBits::eTransferSrc,
    HostDeviceReadableBuffer::memory,
    HostDeviceReadableBuffer::flags,
    HostDeviceReadableBuffer::warnIfHost,
};

} // namespace

BufferManager::~BufferManager() {
    if (initialized()) {
        logError("BufferManager: destroyed without shutdown() - the mega-buffers "
                 "have leaked. Call shutdown() while the device is still alive.");
    }
}

bool BufferManager::init(VulkanContext& ctx, const GPUBufferCapacities& gpuBufferCapacities) {
    if (initialized()) {
        logError("BufferManager: init called twice");
        return false;
    }
    allocator_ = ctx.allocator();
    device_    = ctx.device();

    for (size_t i = 0; i < STATIC_BUFFER_COUNT; ++i) {
        if (gpuBufferCapacities.staticBytes[i] == 0) continue;   // opted out

        const BufferSpec& spec = STATIC_SPECS[i];
        if (!createBuffer(staticBuffers_[i], gpuBufferCapacities.staticBytes[i], spec.usage,
                          spec.memory, spec.flags, spec.warnIfHost, spec.name)) {
            shutdown();
            return false;
        }
    }

    if (!createBuffer(upload_, gpuBufferCapacities.upload, UPLOAD_SPEC.usage, UPLOAD_SPEC.memory,
                      UPLOAD_SPEC.flags, UPLOAD_SPEC.warnIfHost,
                      UPLOAD_SPEC.name)) {
        shutdown();
        return false;
    }

    return true;
}

bool BufferManager::createBuffer(MegaBuffer& out, vk::DeviceSize capacity,
                              vk::BufferUsageFlags usage, vma::MemoryUsage memory,
                              vma::AllocationCreateFlags flags, bool warnIfHost,
                              const char* debugName) {
    if (capacity == 0) {
        logError(std::string("BufferManager: zero capacity for ") + debugName);
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
    const vk::Result result = allocator_.createBuffer(
        &bufferInfo, &allocationInfo,
        &out.buffer, &out.allocation, &resultInfo);
    if (result != vk::Result::eSuccess) {
        logError(std::string("BufferManager: vmaCreateBuffer failed for ") +
                 debugName + ": VkResult " + vk::to_string(result));
        out = {};
        return false;
    }

    out.capacity = bufferInfo.size;
    out.mapped   = resultInfo.pMappedData;
    allocator_.setAllocationName(out.allocation, debugName);

    /* DeviceHostMapped asks for VRAM the CPU can write; without BAR space VMA
     * hands back host memory instead, and every shader read then crosses PCIe. */
    if (warnIfHost) {
        const vk::MemoryPropertyFlags properties =
            allocator_.getAllocationMemoryProperties(out.allocation);
        if (!(properties & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
            logError(std::string("BufferManager: ") + debugName +
                     " asked for device-local mapped memory and got host memory; "
                     "shader reads of it cross PCIe");
        }
    }

    vma::VirtualBlockCreateInfo virtualBlockInfo{};
    virtualBlockInfo.size = out.capacity;   // byte-addressed
    if (vma::createVirtualBlock(&virtualBlockInfo, &out.virtualBlock) != vk::Result::eSuccess) {
        logError(std::string("BufferManager: vmaCreateVirtualBlock failed for ") +
                 debugName);
        destroyBuffer(out);
        return false;
    }

    if (!!(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)) {
        vk::BufferDeviceAddressInfo addressInfo{};
        addressInfo.buffer = out.buffer;
        out.deviceAddress  = device_.getBufferAddress(addressInfo);
        if (out.deviceAddress == 0) {
            logError(std::string("BufferManager: no device address for ") + debugName);
            destroyBuffer(out);
            return false;
        }
    }

    return true;
}

void BufferManager::destroyBuffer(MegaBuffer& buffer) {
    if (buffer.virtualBlock) {
        buffer.virtualBlock.clearVirtualBlock();
        buffer.virtualBlock.destroy();
    }
    if (buffer.buffer)
        allocator_.destroyBuffer(buffer.buffer, buffer.allocation);
    buffer = {};
}

void BufferManager::shutdown() {
    if (!allocator_) return;

    retired_.clear();
    destroyBuffer(upload_);

    for (auto& buffer : staticBuffers_)
        destroyBuffer(buffer);

    device_           = nullptr;
    allocator_        = nullptr;
    retirementSerial_ = 0;
}

BufferManager::RawAllocation BufferManager::allocate(
    MegaBuffer& buffer, vk::DeviceSize bytes, vk::DeviceSize alignment) {
    if (!buffer.virtualBlock || bytes == 0) return {};

    vma::VirtualAllocationCreateInfo allocationInfo{};
    allocationInfo.size      = bytes;
    allocationInfo.alignment = alignment;

    vma::VirtualAllocation allocation = nullptr;
    vk::DeviceSize offset = 0;
    if (buffer.virtualBlock.virtualAllocate(&allocationInfo,
                           &allocation, &offset) != vk::Result::eSuccess)
        return {};

    return RawAllocation{
        BufferRegion{buffer.buffer, offset, bytes},
        buffer.deviceAddress ? buffer.deviceAddress + offset : 0,
        buffer.mapped ? static_cast<std::byte*>(buffer.mapped) + offset : nullptr,
        buffer.virtualBlock,
        allocation,
    };
}

BufferManager::RawAllocation BufferManager::allocateStaticRaw(
    StaticBufferKind kind, vk::DeviceSize bytes, vk::DeviceSize alignment) {
    if (kind == StaticBufferKind::Count) return {};

    RawAllocation raw = allocate(staticBuffers_[index(kind)], bytes, alignment);
    if (!raw.region)
        logError(std::string("BufferManager: ") + STATIC_SPECS[index(kind)].name +
                 " is full");
    return raw;
}

MappedSpan<std::byte> BufferManager::allocateUpload(vk::DeviceSize bytes,
                                                 vk::DeviceSize alignment) {
    if (bytes > upload_.capacity) {
        logError("BufferManager: upload request of " + std::to_string(bytes) +
                 " bytes exceeds the whole " + std::to_string(upload_.capacity) +
                 " byte upload buffer; resetUpload() will not help, raise "
                 "GPUBufferCapacities::upload or split the transfer");
        return {};
    }

    const RawAllocation raw = allocate(upload_, bytes, alignment);
    if (!raw.region) {
        logError("BufferManager: upload buffer is full; submit the open UploadBatch "
                 "to reclaim it");
        return {};
    }

    return spanOf<HostDeviceReadableBuffer, std::byte>(
        raw, static_cast<uint32_t>(bytes));
}

vk::DeviceAddress BufferManager::staticBaseAddress(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return 0;
    return staticBuffers_[index(kind)].deviceAddress;
}

BufferUsage BufferManager::staticUsage(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return {};

    const MegaBuffer& buffer = staticBuffers_[index(kind)];

    vma::Statistics statistics{};
    if (buffer.virtualBlock)
        statistics = buffer.virtualBlock.getVirtualBlockStatistics();

    return BufferUsage{STATIC_SPECS[index(kind)].name, buffer.capacity,
                       statistics.allocationBytes};
}

void BufferManager::retire(vma::VirtualBlock block,
                        vma::VirtualAllocation allocation) {
    if (!initialized() || !block || !allocation) return;
    retired_.push_back({block, allocation, retirementSerial_});
}

void BufferManager::collect(uint64_t completedSerial) {
    auto out = retired_.begin();
    for (auto it = retired_.begin(); it != retired_.end(); ++it) {
        if (it->serial <= completedSerial)
            it->block.virtualFree(it->virtualAllocation);
        else
            *out++ = *it;
    }
    retired_.erase(out, retired_.end());
}

void BufferManager::resetUpload() {
    if (upload_.virtualBlock) upload_.virtualBlock.clearVirtualBlock();
}
