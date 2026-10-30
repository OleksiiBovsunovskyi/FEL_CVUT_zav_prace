module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

export module AllocatedBuffer;

import vulkan;
import vk_mem_alloc;
import VulkanContext;
import GPUTypes;
import Logger;
export import Resource;
export import BufferTypes;

/**
 * Owns one vk::Buffer, its VMA allocation, and the access its contents are in.
 * Placement decides the memory type, the VMA flags, and the span type
 * AllocatedBuffer::span() yields.
 * The buffer is destroyed on destruction.
 * @note The device and the VMA allocator must outlive this.
 */
export template <typename Placement>
class AllocatedBuffer : public IResource {
public:
    AllocatedBuffer() = default;
    ~AllocatedBuffer() override { destroy(); }

    AllocatedBuffer(const AllocatedBuffer&)            = delete;
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;

    AllocatedBuffer(AllocatedBuffer&& other) noexcept : IResource(std::move(other)) { swap(other); }
    AllocatedBuffer& operator=(AllocatedBuffer&& other) =delete;

    /**
     * Creates the buffer at the given capacity.
     * @param ctx supplies the device and the VMA allocator.
     * @param capacity size in bytes.
     * @param usage buffer usage flags.
     * @param debugName name reported by VMA and by this class's errors.
     * @return false if called twice, on a zero capacity, or on a failed allocation.
     * @note A device address is taken when `usage` carries eShaderDeviceAddress.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, vk::DeviceSize capacity,
                            vk::BufferUsageFlags usage, const char* debugName);

    /// @return true while a buffer is owned.
    [[nodiscard]] explicit operator bool() const { return buffer_ != nullptr; }

    /// @return the capacity in bytes.
    [[nodiscard]] vk::DeviceSize capacity() const { return capacity_; }

    /// @return the whole buffer as a range.
    [[nodiscard]] BufferRegion region() const {
        return buffer_ ? BufferRegion{buffer_, 0, capacity_} : BufferRegion{};
    }

    /**
     * @param offset byte offset into the buffer.
     * @param size size in bytes.
     * @return the range, or an empty one when it leaves the buffer.
     */
    [[nodiscard]] BufferRegion region(vk::DeviceSize offset,
                                      vk::DeviceSize size) const {
        if (!buffer_ || size == 0 || offset > capacity_ ||
            size > capacity_ - offset)
            return {};
        return BufferRegion{buffer_, offset, size};
    }

    /**
     * @param offset byte offset into the buffer.
     * @return the shader address of that offset, null when no device address
     *         was taken.
     */
    template <typename T>
    [[nodiscard]] GpuPtr<T> gpuAddress(vk::DeviceSize offset = 0) const {
        return GpuPtr<T>{deviceAddress_.address ? deviceAddress_.address + offset : 0};
    }

    /**
     * @param count records the span covers.
     * @param offset byte offset into the buffer.
     * @return the placement's span type, empty when the records do not fit.
     */
    template <typename T>
    [[nodiscard]] typename Placement::template Span<T> span(
        uint32_t count, vk::DeviceSize offset = 0) const {
        typename Placement::template Span<T> result{};
        result.region = region(offset, vk::DeviceSize{count} * sizeof(T));
        if (!result.region) return {};

        result.gpu = GpuSpan<T>{gpuAddress<T>(offset), count};
        if constexpr (Placement::hostWritable)
            result.host = reinterpret_cast<T*>(mapped_ + offset);
        return result;
    }

    /**
     * Copies records into the mapping.
     * @param source records to copy from.
     * @param count records to copy.
     * @param offset byte offset into the buffer.
     * @return false when the records do not fit.
     */
    template <typename T>
    [[nodiscard]] bool write(const T* source, uint32_t count,
                             vk::DeviceSize offset = 0) {
        static_assert(Placement::hostWritable,
                      "AllocatedBuffer::write() needs a host-writable placement");
        const vk::DeviceSize size = vk::DeviceSize{count} * sizeof(T);
        if (!source || !mapped_ || !region(offset, size)) return false;

        std::memcpy(mapped_ + offset, source, size);
        return true;
    }

protected:
    /// @return the mapping, nullptr when the allocation is not mapped.
    [[nodiscard]] std::byte* mapped() const { return mapped_; }

    /// Destroys the buffer.
    void release() override;

    void collect(const ResourceAccess& targetAccess, BarrierList& list) override
    {
        assert(buffer_ && "AllocatedBuffer::collect: no buffer to barrier");

        vk::BufferMemoryBarrier2 barrier{};
        const std::optional<ResourceAccess> previous = exchangeAccess(targetAccess);
        if (!previous) return; //No barrier needed
        
        barrier.srcStageMask        = previous->stageMask;
        barrier.srcAccessMask       = previous->accessMask;
        barrier.dstStageMask        = targetAccess.stageMask;
        barrier.dstAccessMask       = targetAccess.accessMask;
        barrier.buffer              = buffer_;
        barrier.offset              = 0;
        barrier.size                = capacity_;
        
        list.add(barrier);
    }

private:
    void swap(AllocatedBuffer& other) noexcept {
        std::swap(allocator_, other.allocator_);
        std::swap(buffer_, other.buffer_);
        std::swap(allocation_, other.allocation_);
        std::swap(capacity_, other.capacity_);
        std::swap(deviceAddress_, other.deviceAddress_);
        std::swap(mapped_, other.mapped_);
    }

    vma::Allocator    allocator_     = nullptr;
    vk::Buffer        buffer_        = nullptr;
    vma::Allocation   allocation_    = nullptr;
    vk::DeviceSize    capacity_      = 0;
    GpuPtr<std::byte> deviceAddress_{};
    std::byte*        mapped_        = nullptr;
};

/* ------------------------------------------------------------------------ */

template <typename Placement>
bool AllocatedBuffer<Placement>::init(VulkanContext& ctx, vk::DeviceSize capacity,
                                      vk::BufferUsageFlags usage,
                                      const char* debugName) {
    const std::string name = debugName ? debugName : "buffer";
    if (buffer_) {
        logError("AllocatedBuffer: init called twice for " + name);
        return false;
    }
    if (capacity == 0) {
        logError("AllocatedBuffer: zero capacity for " + name);
        return false;
    }

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size        = capacity;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    vma::AllocationCreateInfo allocationInfo{};
    allocationInfo.usage = Placement::memory;
    allocationInfo.flags = Placement::flags;

    vma::AllocationInfo resultInfo{};
    allocator_ = ctx.allocator();
    const vk::Result result = allocator_.createBuffer(
        &bufferInfo, &allocationInfo, &buffer_, &allocation_, &resultInfo);
    if (result != vk::Result::eSuccess) {
        logError("AllocatedBuffer: vmaCreateBuffer failed for " + name +
                 ": VkResult " + vk::to_string(result));
        destroy();
        return false;
    }

    capacity_  = capacity;
    mapped_    = static_cast<std::byte*>(resultInfo.pMappedData);
    allocator_.setAllocationName(allocation_, debugName);

    if constexpr (Placement::hostWritable) {
        if (!mapped_) {
            logError("AllocatedBuffer: " + name + " is not mapped");
            destroy();
            return false;
        }
    }

    /* Without BAR space VMA hands back host memory, and shader reads of it then
     * cross PCIe. */
    if constexpr (Placement::warnIfHost) {
        const vk::MemoryPropertyFlags properties =
            allocator_.getAllocationMemoryProperties(allocation_);
        if (!(properties & vk::MemoryPropertyFlagBits::eDeviceLocal))
            logError("AllocatedBuffer: " + name +
                     " requested device-local mapped memory and received host "
                     "memory; shader reads of it cross PCIe");
    }

    if (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        vk::BufferDeviceAddressInfo addressInfo{};
        addressInfo.buffer = buffer_;
        deviceAddress_ = GpuPtr<std::byte>{ctx.device().getBufferAddress(addressInfo)};
        if (deviceAddress_.address == 0) {
            logError("AllocatedBuffer: no device address for " + name);
            destroy();
            return false;
        }
    }
    return true;
}

template <typename Placement>
void AllocatedBuffer<Placement>::release() {
    if (buffer_ && allocator_) allocator_.destroyBuffer(buffer_, allocation_);
    allocator_     = nullptr;
    buffer_        = nullptr;
    allocation_    = nullptr;
    capacity_      = 0;
    deviceAddress_ = {};
    mapped_        = nullptr;
}

