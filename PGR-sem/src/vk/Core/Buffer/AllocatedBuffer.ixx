module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>

export module AllocatedBuffer;

import VulkanContext;
import GPUTypes;
import Logger;
export import BufferTypes;

/**
 * Stage and access mask a buffer's contents were last left in.
 * A default-constructed value means the contents are undefined.
 */
export struct BufferAccess {
    vk::PipelineStageFlags2 stageMask  = vk::PipelineStageFlagBits2::eNone;
    vk::AccessFlags2        accessMask = vk::AccessFlagBits2::eNone;

    /// @return true once some access has been recorded.
    [[nodiscard]] bool defined() const {
        return stageMask != vk::PipelineStageFlagBits2::eNone;
    }

    /// @return true when every access bit is a known read.
    [[nodiscard]] bool readOnly() const {
        constexpr vk::AccessFlags2 reads =
            vk::AccessFlagBits2::eIndirectCommandRead |
            vk::AccessFlagBits2::eIndexRead |
            vk::AccessFlagBits2::eVertexAttributeRead |
            vk::AccessFlagBits2::eUniformRead |
            vk::AccessFlagBits2::eInputAttachmentRead |
            vk::AccessFlagBits2::eShaderRead |
            vk::AccessFlagBits2::eShaderStorageRead |
            vk::AccessFlagBits2::eShaderSampledRead |
            vk::AccessFlagBits2::eColorAttachmentRead |
            vk::AccessFlagBits2::eDepthStencilAttachmentRead |
            vk::AccessFlagBits2::eTransferRead |
            vk::AccessFlagBits2::eHostRead |
            vk::AccessFlagBits2::eMemoryRead;
        return !(accessMask & ~reads);
    }
};

/// Records one pipelineBarrier2 covering every barrier given.
export inline void recordBarriers(
    vk::CommandBuffer cmd, std::span<const vk::BufferMemoryBarrier2> barriers) {
    if (barriers.empty()) return;

    vk::DependencyInfo dependency{};
    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pBufferMemoryBarriers    = barriers.data();
    cmd.pipelineBarrier2(dependency);
}

/**
 * Records into a command buffer, submits it, and blocks until the GPU is done.
 */
export template <typename T>
concept TransferSubmitter = requires(T& submitter) {
    { submitter.begin() } -> std::same_as<vk::CommandBuffer>;
    { submitter.submitAndWait() } -> std::same_as<bool>;
};

/**
 * Owns one vk::Buffer, its VMA allocation, and the access its contents are in.
 * Placement decides the memory type, the VMA flags, and the span type
 * AllocatedBuffer::span() yields.
 * The buffer is destroyed on destruction.
 * @note The device and the VMA allocator must outlive this.
 */
export template <typename Placement>
class AllocatedBuffer {
public:
    AllocatedBuffer() = default;
    ~AllocatedBuffer() { destroy(); }

    AllocatedBuffer(const AllocatedBuffer&)            = delete;
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;

    AllocatedBuffer(AllocatedBuffer&& other) noexcept { swap(other); }
    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept {
        if (this != &other) {
            destroy();
            swap(other);
        }
        return *this;
    }

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

    /// Destroys the buffer. Safe to call twice.
    void destroy();

    /// @return true while a buffer is owned.
    [[nodiscard]] explicit operator bool() const { return buffer_ != nullptr; }

    /// @return the capacity in bytes.
    [[nodiscard]] vk::DeviceSize capacity() const { return capacity_; }

    /// @return the name passed to AllocatedBuffer::init().
    [[nodiscard]] const char* debugName() const { return debugName_; }

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
                      "AllocatedBuffer::write() needs a host-writable placement; "
                      "a DeviceOnlyBuffer is filled with AllocatedBuffer::upload()");
        const vk::DeviceSize size = vk::DeviceSize{count} * sizeof(T);
        if (!source || !mapped_ || !region(offset, size)) return false;

        std::memcpy(mapped_ + offset, source, size);
        return true;
    }

    /**
     * Records a copy into this buffer.
     * @param cmd command buffer to record into.
     * @param source range to copy from.
     * @param offset byte offset into this buffer.
     * @note The source must stay alive until `cmd` completes.
     */
    void upload(vk::CommandBuffer cmd, const BufferRegion& source,
                vk::DeviceSize offset = 0) const;

    /**
     * Records a copy out of this buffer.
     * @param cmd command buffer to record into.
     * @param destination host-readable range to copy into.
     * @note The destination holds the data only once `cmd` completes.
     */
    void readNonBlocking(vk::CommandBuffer cmd,
                         const BufferRegion& destination) const;

    /**
     * Copies out of this buffer and blocks until the GPU is done.
     * @param submitter owns the command buffer, the queue and the fence.
     * @param destination host-readable range to copy into.
     * @return false when the submission failed.
     */
    template <TransferSubmitter Batch>
    [[nodiscard]] bool readBlocking(Batch& submitter,
                                    const BufferRegion& destination) const {
        const vk::CommandBuffer cmd = submitter.begin();
        if (!cmd) return false;

        readNonBlocking(cmd, destination);
        return submitter.submitAndWait();
    }

    /**
     * Declares the access the following commands need, and records it.
     * @param next stage and access those commands use.
     * @return the barrier to record, nothing when the recorded access already
     *         covers it.
     * @note The barrier must reach the command buffer before those commands.
     */
    [[nodiscard]] std::optional<vk::BufferMemoryBarrier2> use(BufferAccess next);

    /// @return the access recorded by the last AllocatedBuffer::use().
    [[nodiscard]] BufferAccess access() const { return access_; }

    /**
     * Forgets the recorded access.
     * @note Legal once the GPU has finished every submission touching this.
     */
    void resetAccess() { access_ = {}; }

protected:
    /// @return the mapping, nullptr when the allocation is not mapped.
    [[nodiscard]] std::byte* mapped() const { return mapped_; }

private:
    void swap(AllocatedBuffer& other) noexcept {
        std::swap(allocator_, other.allocator_);
        std::swap(buffer_, other.buffer_);
        std::swap(allocation_, other.allocation_);
        std::swap(capacity_, other.capacity_);
        std::swap(deviceAddress_, other.deviceAddress_);
        std::swap(mapped_, other.mapped_);
        std::swap(debugName_, other.debugName_);
        std::swap(access_, other.access_);
    }

    vma::Allocator    allocator_     = nullptr;
    vk::Buffer        buffer_        = nullptr;
    vma::Allocation   allocation_    = nullptr;
    vk::DeviceSize    capacity_      = 0;
    GpuPtr<std::byte> deviceAddress_{};
    std::byte*        mapped_        = nullptr;
    const char*       debugName_     = "";
    BufferAccess      access_{};
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
    debugName_ = debugName;
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
void AllocatedBuffer<Placement>::destroy() {
    if (buffer_ && allocator_) allocator_.destroyBuffer(buffer_, allocation_);
    allocator_     = nullptr;
    buffer_        = nullptr;
    allocation_    = nullptr;
    capacity_      = 0;
    deviceAddress_ = {};
    mapped_        = nullptr;
    debugName_     = "";
    access_        = {};
}

template <typename Placement>
void AllocatedBuffer<Placement>::upload(vk::CommandBuffer cmd,
                                        const BufferRegion& source,
                                        vk::DeviceSize offset) const {
    const BufferRegion destination = region(offset, source.size);
    if (!source || !destination) return;

    const vk::BufferCopy copy{source.offset, destination.offset, source.size};
    cmd.copyBuffer(source.buffer, destination.buffer, 1, &copy);
}

template <typename Placement>
void AllocatedBuffer<Placement>::readNonBlocking(
    vk::CommandBuffer cmd, const BufferRegion& destination) const {
    const BufferRegion source = region(0, destination.size);
    if (!source || !destination) return;

    const vk::BufferCopy copy{source.offset, destination.offset, destination.size};
    cmd.copyBuffer(source.buffer, destination.buffer, 1, &copy);
}

template <typename Placement>
std::optional<vk::BufferMemoryBarrier2> AllocatedBuffer<Placement>::use(
    BufferAccess next) {
    const BufferAccess previous     = access_;
    const bool         readAfterRead = previous.readOnly() && next.readOnly();

    if (readAfterRead) {
        access_.stageMask  |= next.stageMask;
        access_.accessMask |= next.accessMask;
    } else {
        access_ = next;
    }

    if (!previous.defined()) return std::nullopt;
    if (readAfterRead && (previous.stageMask & next.stageMask) == next.stageMask)
        return std::nullopt;

    vk::BufferMemoryBarrier2 barrier{};
    barrier.srcStageMask        = previous.stageMask;
    barrier.srcAccessMask       = previous.accessMask;
    barrier.dstStageMask        = next.stageMask;
    barrier.dstAccessMask       = next.accessMask;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.buffer              = buffer_;
    barrier.offset              = 0;
    barrier.size                = capacity_;
    return barrier;
}
