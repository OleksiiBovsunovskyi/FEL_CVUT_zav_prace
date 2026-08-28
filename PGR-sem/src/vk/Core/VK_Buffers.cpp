module;
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstddef>
#include <string>
#include <utility>

module VK_Buffers;

import GPUTypes;
import Logger;

namespace {

constexpr size_t index(StaticBufferKind kind) {
    return static_cast<size_t>(kind);
}

constexpr size_t index(FrameBufferKind kind) {
    return static_cast<size_t>(kind);
}

/* Specs and capacities come from the traits in VK_Buffers.ixx. */
    
} // namespace

VK_buffers::~VK_buffers() {
    if (initialized()) {
        logError("VK_buffers: destroyed without shutdown() - the mega-buffers "
                 "have leaked. Call shutdown() while the device is still alive.");
    }
}

VkDeviceSize VK_buffers::staticStride(StaticBufferKind kind) {
    if (kind == StaticBufferKind::Count) return 1;
    return STATIC_BUFFER_SPECS[index(kind)].stride;
}

BufferAllocation::BufferAllocation(
    BufferSlice slice, VK_buffers* owner, VmaVirtualBlock block,
    VmaVirtualAllocation allocation)
    : slice_(slice),
      owner_(owner),
      block_(block),
      virtualAllocation_(allocation) {}

BufferAllocation::BufferAllocation(BufferAllocation&& other) noexcept
    : slice_(other.slice_),
      owner_(std::exchange(other.owner_, nullptr)),
      block_(std::exchange(other.block_, VK_NULL_HANDLE)),
      virtualAllocation_(
          std::exchange(other.virtualAllocation_, VK_NULL_HANDLE)) {
    other.slice_ = {};
}

BufferAllocation& BufferAllocation::operator=(
    BufferAllocation&& other) noexcept {
    if (this == &other) return *this;
    release();
    slice_ = other.slice_;
    owner_ = std::exchange(other.owner_, nullptr);
    block_ = std::exchange(other.block_, VK_NULL_HANDLE);
    virtualAllocation_ =
        std::exchange(other.virtualAllocation_, VK_NULL_HANDLE);
    other.slice_ = {};
    return *this;
}

BufferAllocation::~BufferAllocation() {
    release();
}

void BufferAllocation::release() {
    if (owner_ && virtualAllocation_)
        owner_->retire(block_, virtualAllocation_);
    slice_ = {};
    owner_ = nullptr;
    block_ = VK_NULL_HANDLE;
    virtualAllocation_ = VK_NULL_HANDLE;
}

bool VK_buffers::init(VulkanContext& ctx) {
    return init(ctx, GPUBufferCapacities{});
}

bool VK_buffers::init(VulkanContext& ctx, const GPUBufferCapacities& c) {
    if (initialized()) {
        logError("VK_buffers: init called twice");
        return false;   
    }
    if (c.framesInFlight == 0) {
        logError("VK_buffers: framesInFlight must be greater than zero");
        return false;
    }

    allocator_ = ctx.allocator();
    device_    = ctx.device();
    frameBuffers_.resize(c.framesInFlight);

    for (size_t i = 0; i < STATIC_COUNT; ++i) {
        const VkDeviceSize capacity = c.staticBytes[i];
        if (capacity == 0) continue;   // opted out

        const StaticBufferSpec& spec = STATIC_BUFFER_SPECS[i];
        if (!createBuffer(staticBuffers_[i], capacity, spec.stride,
                          GPU_DATA_USAGE,
                          VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, spec.name)) {
            shutdown();
            return false;
        }
    }

    for (uint32_t frame = 0; frame < c.framesInFlight; ++frame) {
        for (size_t i = 0; i < FRAME_COUNT; ++i) {
            const VkDeviceSize capacity = c.frameBytes[i];
            if (capacity == 0) continue;   // opted out

            const FrameBufferSpec& spec = FRAME_BUFFER_SPECS[i];
            const std::string name =
                std::string(spec.name) + " [frame " + std::to_string(frame) + "]";

            /* Stride 1: raw bytes. */
            if (!createBuffer(frameBuffers_[frame][i], capacity, 1, spec.usage,
                              spec.hostWritable ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST
                                                : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                              spec.hostWritable
                                  ? (VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                     VMA_ALLOCATION_CREATE_MAPPED_BIT)
                                  : 0,
                              name.c_str())) {
                shutdown();
                return false;
            }
        }
    }

    if (!createBuffer(upload_, c.upload, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      "upload staging")) {
        shutdown();
        return false;
    }

    return true;
}

bool VK_buffers::createBuffer(MegaBuffer& out, VkDeviceSize capacity,
                              VkDeviceSize stride,
                              VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                              VmaAllocationCreateFlags allocationFlags,
                              const char* debugName) {
    if (capacity == 0 || stride == 0) {
        logError(std::string("VK_buffers: zero capacity or stride for ") + debugName);
        return false;
    }

    /* Block units are elements; VMA alignment must be a power of two. */
    const VkDeviceSize elementCapacity = capacity / stride;
    if (elementCapacity == 0) {
        logError(std::string("VK_buffers: capacity smaller than one element for ") +
                 debugName);
        return false;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = elementCapacity * stride;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = allocationFlags;

    VmaAllocationInfo resultInfo{};
    const VkResult result = vmaCreateBuffer(
        allocator_, &bufferInfo, &allocationInfo,
        &out.buffer, &out.allocation, &resultInfo);
    if (result != VK_SUCCESS) {
        logError(std::string("VK_buffers: vmaCreateBuffer failed for ") +
                 debugName + ": VkResult " + std::to_string(result));
        out = {};
        return false;
    }

    out.capacity = bufferInfo.size;
    out.stride   = stride;
    out.mapped   = resultInfo.pMappedData;
    vmaSetAllocationName(allocator_, out.allocation, debugName);

    VmaVirtualBlockCreateInfo virtualBlockInfo{};
    virtualBlockInfo.size = elementCapacity;
    if (vmaCreateVirtualBlock(&virtualBlockInfo, &out.virtualBlock) != VK_SUCCESS) {
        logError(std::string("VK_buffers: vmaCreateVirtualBlock failed for ") +
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
            logError(std::string("VK_buffers: no device address for ") + debugName);
            destroyBuffer(out);
            return false;
        }
    }

    return true;
}

void VK_buffers::destroyBuffer(MegaBuffer& buffer) {
    if (buffer.virtualBlock) {
        vmaClearVirtualBlock(buffer.virtualBlock);
        vmaDestroyVirtualBlock(buffer.virtualBlock);
    }
    if (buffer.buffer)
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    buffer = {};
}

void VK_buffers::shutdown() {
    if (!allocator_) return;

    retired_.clear();
    destroyBuffer(upload_);
    for (auto& frame : frameBuffers_)
        for (auto& buffer : frame)
            destroyBuffer(buffer);
    frameBuffers_.clear();

    for (auto& buffer : staticBuffers_)
        destroyBuffer(buffer);

    device_    = VK_NULL_HANDLE;
    allocator_ = nullptr;
    retirementSerial_ = 0;
}

VK_buffers::RawAllocation VK_buffers::allocate(
    MegaBuffer& buffer, VkDeviceSize units, VkDeviceSize alignment) {
    if (!buffer.virtualBlock || units == 0) return {};

    VmaVirtualAllocationCreateInfo allocationInfo{};
    allocationInfo.size      = units;
    allocationInfo.alignment = alignment;

    VmaVirtualAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize unitOffset = 0;
    if (vmaVirtualAllocate(buffer.virtualBlock, &allocationInfo,
                           &allocation, &unitOffset) != VK_SUCCESS)
        return {};

    const VkDeviceSize byteOffset = unitOffset * buffer.stride;
    const VkDeviceSize byteSize   = units * buffer.stride;

    return RawAllocation{
        BufferSlice{
            buffer.buffer,
            byteOffset,
            byteSize,
            static_cast<uint32_t>(unitOffset),
            buffer.deviceAddress ? buffer.deviceAddress + byteOffset : 0,
            buffer.mapped
                ? static_cast<std::byte*>(buffer.mapped) + byteOffset
                : nullptr,
        },
        buffer.virtualBlock,
        allocation,
    };
}

void VK_buffers::retire(VmaVirtualBlock block,
                        VmaVirtualAllocation allocation) {
    if (!initialized() || !block || !allocation) return;
    retired_.push_back({block, allocation, retirementSerial_});
}

void VK_buffers::collect(uint64_t completedSerial) {
    auto out = retired_.begin();
    for (auto it = retired_.begin(); it != retired_.end(); ++it) {
        if (it->serial <= completedSerial)
            vmaVirtualFree(it->block, it->virtualAllocation);
        else
            *out++ = *it;
    }
    retired_.erase(out, retired_.end());
}

BufferAllocation VK_buffers::allocateStatic(
    StaticBufferKind kind, uint32_t elementCount) {
    if (kind == StaticBufferKind::Count) return {};

    /* Alignment is in elements. */
    auto [slice, block, virtualAllocation] =
        allocate(staticBuffers_[index(kind)], elementCount, 1);
    if (!slice) {
        logError("VK_buffers: static mega-buffer is full");
        return {};
    }
    return BufferAllocation{
        slice,
        this,
        block,
        virtualAllocation,
    };
}

BufferSlice VK_buffers::allocateFrame(uint32_t frameIndex, FrameBufferKind kind,
                                      VkDeviceSize bytes, VkDeviceSize alignment) {
    if (frameIndex >= frameBuffers_.size() || kind == FrameBufferKind::Count)
        return {};
    RawAllocation allocation =
        allocate(frameBuffers_[frameIndex][index(kind)], bytes, alignment);
    if (!allocation.slice)
        logError("VK_buffers: per-frame mega-buffer is full");
    return allocation.slice;
}

BufferSlice VK_buffers::allocateUpload(VkDeviceSize bytes, VkDeviceSize alignment) {
    if (bytes > upload_.capacity) {
        logError("VK_buffers: staging request of " + std::to_string(bytes) +
                 " bytes exceeds the whole " + std::to_string(upload_.capacity) +
                 " byte staging buffer; resetUpload() will not help, raise "
                 "GPUBufferCapacities::upload or split the transfer");
        return {};
    }

    const RawAllocation allocation = allocate(upload_, bytes, alignment);
    if (!allocation.slice)
        logError("VK_buffers: staging buffer is full; submit the open UploadBatch "
                 "to reclaim it");
    return allocation.slice;
}

MegaBufferView VK_buffers::view(const MegaBuffer& buffer) {
    VmaStatistics statistics{};
    if (buffer.virtualBlock)
        vmaGetVirtualBlockStatistics(buffer.virtualBlock, &statistics);

    return MegaBufferView{
        buffer.buffer,
        buffer.capacity,
        /* Statistics are in elements. */
        statistics.allocationBytes * buffer.stride,
        buffer.stride,
        buffer.deviceAddress,
        buffer.mapped,
    };
}

MegaBufferView VK_buffers::staticBuffer(StaticBufferKind kind) const {
    if (kind == StaticBufferKind::Count) return {};
    return view(staticBuffers_[index(kind)]);
}

MegaBufferView VK_buffers::frameBuffer(uint32_t frameIndex,
                                       FrameBufferKind kind) const {
    if (frameIndex >= frameBuffers_.size() || kind == FrameBufferKind::Count)
        return {};
    return view(frameBuffers_[frameIndex][index(kind)]);
}

MegaBufferView VK_buffers::uploadBuffer() const {
    return view(upload_);
}

void VK_buffers::resetFrame(uint32_t frameIndex) {
    if (frameIndex >= frameBuffers_.size()) return;
    for (auto& buffer : frameBuffers_[frameIndex])
        if (buffer.virtualBlock) vmaClearVirtualBlock(buffer.virtualBlock);
}

void VK_buffers::resetUpload() {
    if (upload_.virtualBlock) vmaClearVirtualBlock(upload_.virtualBlock);
}
