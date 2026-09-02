module;
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

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

constexpr size_t index(FrameSlotBufferKind kind) {
    return static_cast<size_t>(kind);
}

/// Runtime form of the traits, for the loops that walk every buffer.
struct BufferSpec {
    const char*              name       = "";
    VkBufferUsageFlags       usage      = 0;
    VmaMemoryUsage           memory     = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags flags      = 0;
    bool                     warnIfHost = false;
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
constexpr auto FRAME_SPECS = specsOf<FrameSlotBufferTraits, FrameSlotBufferKind>(
    std::make_index_sequence<FRAME_SLOT_BUFFER_COUNT>{});

constexpr BufferSpec UPLOAD_SPEC{
    "upload",
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
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

bool BufferManager::init(VulkanContext& ctx, const GPUBufferCapacities& c) {
    if (initialized()) {
        logError("BufferManager: init called twice");
        return false;
    }
    if (c.framesInFlight == 0) {
        logError("BufferManager: framesInFlight must be greater than zero");
        return false;
    }

    allocator_ = ctx.allocator();
    device_    = ctx.device();
    frameSlotBuffers_.resize(c.framesInFlight);

    for (size_t i = 0; i < STATIC_BUFFER_COUNT; ++i) {
        if (c.staticBytes[i] == 0) continue;   // opted out

        const BufferSpec& spec = STATIC_SPECS[i];
        if (!createBuffer(staticBuffers_[i], c.staticBytes[i], spec.usage,
                          spec.memory, spec.flags, spec.warnIfHost, spec.name)) {
            shutdown();
            return false;
        }
    }

    for (uint32_t frame = 0; frame < c.framesInFlight; ++frame) {
        for (size_t i = 0; i < FRAME_SLOT_BUFFER_COUNT; ++i) {
            if (c.frameBytes[i] == 0) continue;   // opted out

            const BufferSpec& spec = FRAME_SPECS[i];
            const std::string name =
                std::string(spec.name) + " [frame " + std::to_string(frame) + "]";

            if (!createBuffer(frameSlotBuffers_[frame][i], c.frameBytes[i],
                              spec.usage, spec.memory, spec.flags,
                              spec.warnIfHost, name.c_str())) {
                shutdown();
                return false;
            }
        }
    }

    if (!createBuffer(upload_, c.upload, UPLOAD_SPEC.usage, UPLOAD_SPEC.memory,
                      UPLOAD_SPEC.flags, UPLOAD_SPEC.warnIfHost,
                      UPLOAD_SPEC.name)) {
        shutdown();
        return false;
    }

    return true;
}

bool BufferManager::createBuffer(MegaBuffer& out, VkDeviceSize capacity,
                              VkBufferUsageFlags usage, VmaMemoryUsage memory,
                              VmaAllocationCreateFlags flags, bool warnIfHost,
                              const char* debugName) {
    if (capacity == 0) {
        logError(std::string("BufferManager: zero capacity for ") + debugName);
        return false;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = capacity;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memory;
    allocationInfo.flags = flags;

    VmaAllocationInfo resultInfo{};
    const VkResult result = vmaCreateBuffer(
        allocator_, &bufferInfo, &allocationInfo,
        &out.buffer, &out.allocation, &resultInfo);
    if (result != VK_SUCCESS) {
        logError(std::string("BufferManager: vmaCreateBuffer failed for ") +
                 debugName + ": VkResult " + std::to_string(result));
        out = {};
        return false;
    }

    out.capacity = bufferInfo.size;
    out.mapped   = resultInfo.pMappedData;
    vmaSetAllocationName(allocator_, out.allocation, debugName);

    /* DeviceHostMapped asks for VRAM the CPU can write; without BAR space VMA
     * hands back host memory instead, and every shader read then crosses PCIe. */
    if (warnIfHost) {
        VkMemoryPropertyFlags properties = 0;
        vmaGetAllocationMemoryProperties(allocator_, out.allocation, &properties);
        if ((properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0) {
            logError(std::string("BufferManager: ") + debugName +
                     " asked for device-local mapped memory and got host memory; "
                     "shader reads of it cross PCIe");
        }
    }

    VmaVirtualBlockCreateInfo virtualBlockInfo{};
    virtualBlockInfo.size = out.capacity;   // byte-addressed
    if (vmaCreateVirtualBlock(&virtualBlockInfo, &out.virtualBlock) != VK_SUCCESS) {
        logError(std::string("BufferManager: vmaCreateVirtualBlock failed for ") +
                 debugName);
        destroyBuffer(out);
        return false;
    }

    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = out.buffer;
        out.deviceAddress  = vkGetBufferDeviceAddress(device_, &addressInfo);
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
        vmaClearVirtualBlock(buffer.virtualBlock);
        vmaDestroyVirtualBlock(buffer.virtualBlock);
    }
    if (buffer.buffer)
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    buffer = {};
}

void BufferManager::shutdown() {
    if (!allocator_) return;

    retired_.clear();
    destroyBuffer(upload_);
    for (auto& frame : frameSlotBuffers_)
        for (auto& buffer : frame)
            destroyBuffer(buffer);
    frameSlotBuffers_.clear();

    for (auto& buffer : staticBuffers_)
        destroyBuffer(buffer);

    device_           = VK_NULL_HANDLE;
    allocator_        = nullptr;
    retirementSerial_ = 0;
}

BufferManager::RawAllocation BufferManager::allocate(
    MegaBuffer& buffer, VkDeviceSize bytes, VkDeviceSize alignment) {
    if (!buffer.virtualBlock || bytes == 0) return {};

    VmaVirtualAllocationCreateInfo allocationInfo{};
    allocationInfo.size      = bytes;
    allocationInfo.alignment = alignment;

    VmaVirtualAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    if (vmaVirtualAllocate(buffer.virtualBlock, &allocationInfo,
                           &allocation, &offset) != VK_SUCCESS)
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
    StaticBufferKind kind, VkDeviceSize bytes, VkDeviceSize alignment) {
    if (kind == StaticBufferKind::Count) return {};

    RawAllocation raw = allocate(staticBuffers_[index(kind)], bytes, alignment);
    if (!raw.region)
        logError(std::string("BufferManager: ") + STATIC_SPECS[index(kind)].name +
                 " is full");
    return raw;
}

BufferManager::RawAllocation BufferManager::allocateFrameRaw(
    uint32_t frameIndex, FrameSlotBufferKind kind,
    VkDeviceSize bytes, VkDeviceSize alignment) {
    if (frameIndex >= frameSlotBuffers_.size() || kind == FrameSlotBufferKind::Count)
        return {};

    RawAllocation raw =
        allocate(frameSlotBuffers_[frameIndex][index(kind)], bytes, alignment);
    if (!raw.region)
        logError(std::string("BufferManager: ") + FRAME_SPECS[index(kind)].name +
                 " is full for this frame");
    return raw;
}

MappedSpan<std::byte> BufferManager::allocateUpload(VkDeviceSize bytes,
                                                 VkDeviceSize alignment) {
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

VkDeviceAddress BufferManager::staticBaseAddress(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return 0;
    return staticBuffers_[index(kind)].deviceAddress;
}

BufferUsage BufferManager::staticUsage(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return {};

    const MegaBuffer& buffer = staticBuffers_[index(kind)];

    VmaStatistics statistics{};
    if (buffer.virtualBlock)
        vmaGetVirtualBlockStatistics(buffer.virtualBlock, &statistics);

    return BufferUsage{STATIC_SPECS[index(kind)].name, buffer.capacity,
                       statistics.allocationBytes};
}

void BufferManager::retire(VmaVirtualBlock block,
                        VmaVirtualAllocation allocation) {
    if (!initialized() || !block || !allocation) return;
    retired_.push_back({block, allocation, retirementSerial_});
}

void BufferManager::collect(uint64_t completedSerial) {
    auto out = retired_.begin();
    for (auto it = retired_.begin(); it != retired_.end(); ++it) {
        if (it->serial <= completedSerial)
            vmaVirtualFree(it->block, it->virtualAllocation);
        else
            *out++ = *it;
    }
    retired_.erase(out, retired_.end());
}

void BufferManager::resetFrame(uint32_t frameIndex) {
    if (frameIndex >= frameSlotBuffers_.size()) return;
    for (auto& buffer : frameSlotBuffers_[frameIndex])
        if (buffer.virtualBlock) vmaClearVirtualBlock(buffer.virtualBlock);
}

void BufferManager::resetUpload() {
    if (upload_.virtualBlock) vmaClearVirtualBlock(upload_.virtualBlock);
}
