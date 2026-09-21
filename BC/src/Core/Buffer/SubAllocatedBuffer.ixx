module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

export module SubAllocatedBuffer;

import vulkan;
import vk_mem_alloc;
import VulkanContext;
import GPUTypes;
import Logger;
export import BufferTypes;
export import AllocatedBuffer;

/**
 * Identifies one range reserved inside a SubAllocatedBuffer.
 * The range is not owned; SubAllocatedBuffer::free() releases it.
 * `host` is non-null only for a host-writable placement.
 */
export struct SubAllocationHandle {
    BufferRegion           region{};
    GpuPtr<std::byte>      address{};
    std::byte*             host    = nullptr;
    vma::VirtualAllocation handle  = nullptr;

    [[nodiscard]] explicit operator bool() const { return handle != nullptr; }
};

/**
 * An AllocatedBuffer whose space is handed out in ranges.
 * Ranges are byte-addressed.
 * The virtual block is destroyed on destruction.
 */
export template <typename Placement>
class SubAllocatedBuffer : public AllocatedBuffer<Placement> {
public:
    using Base = AllocatedBuffer<Placement>;

    SubAllocatedBuffer() = default;
    ~SubAllocatedBuffer() { destroy(); }

    SubAllocatedBuffer(const SubAllocatedBuffer&)            = delete;
    SubAllocatedBuffer& operator=(const SubAllocatedBuffer&) = delete;

    SubAllocatedBuffer(SubAllocatedBuffer&& other) noexcept
        : Base(std::move(other)) {
        std::swap(virtualBlock_, other.virtualBlock_);
    }
    SubAllocatedBuffer& operator=(SubAllocatedBuffer&& other) noexcept {
        if (this != &other) {
            destroy();
            Base::operator=(std::move(other));
            std::swap(virtualBlock_, other.virtualBlock_);
        }
        return *this;
    }

    /**
     * Creates the buffer and the virtual block covering it.
     * @param ctx supplies the device and the VMA allocator.
     * @param capacity size in bytes.
     * @param usage buffer usage flags.
     * @param debugName name reported by VMA and by this class's errors.
     * @return false on a failed allocation.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, vk::DeviceSize capacity,
                            vk::BufferUsageFlags usage, const char* debugName);

    /// Releases every range and destroys the buffer. Safe to call twice.
    void destroy();

    /**
     * Reserves a range.
     * @param bytes size to reserve.
     * @param alignment in bytes; must be a power of two.
     * @return the range, empty when the buffer is full.
     */
    [[nodiscard]] SubAllocationHandle allocate(vk::DeviceSize bytes,
                                         vk::DeviceSize alignment);

    /**
     * Releases one range.
     * @param allocation range to release.
     * @note The GPU must be done with the range before this is called.
     */
    void free(const SubAllocationHandle& allocation) {
        if (virtualBlock_ && allocation.handle)
            virtualBlock_.virtualFree(allocation.handle);
    }

    /**
     * Releases every range at once.
     * @note The GPU must be done with every range before this is called.
     */
    void clear() {
        if (virtualBlock_) virtualBlock_.clearVirtualBlock();
    }

    /// @return bytes currently reserved.
    [[nodiscard]] vk::DeviceSize used() const {
        if (!virtualBlock_) return 0;
        return virtualBlock_.getVirtualBlockStatistics().allocationBytes;
    }

    /**
     * Fills in whichever span type the placement declares.
     * @param allocation range the span covers.
     * @param count records in that range.
     */
    template <typename T>
    [[nodiscard]] static typename Placement::template Span<T> spanOf(
        const SubAllocationHandle& allocation, uint32_t count) {
        typename Placement::template Span<T> span{};
        span.region = allocation.region;
        span.gpu    = GpuSpan<T>{GpuPtr<T>{allocation.address.address}, count};
        if constexpr (Placement::hostWritable)
            span.host = reinterpret_cast<T*>(allocation.host);
        return span;
    }

private:
    vma::VirtualBlock virtualBlock_ = nullptr;
};

/* ------------------------------------------------------------------------ */

template <typename Placement>
bool SubAllocatedBuffer<Placement>::init(VulkanContext& ctx,
                                         vk::DeviceSize capacity,
                                         vk::BufferUsageFlags usage,
                                         const char* debugName) {
    if (!Base::init(ctx, capacity, usage, debugName)) return false;

    /* Block units are bytes; VMA alignment must be a power of two. */
    vma::VirtualBlockCreateInfo virtualBlockInfo{};
    virtualBlockInfo.size = capacity;
    if (vma::createVirtualBlock(&virtualBlockInfo, &virtualBlock_) !=
        vk::Result::eSuccess) {
        logError(std::string("SubAllocatedBuffer: vmaCreateVirtualBlock failed for ") +
                 (debugName ? debugName : "buffer"));
        destroy();
        return false;
    }
    return true;
}

template <typename Placement>
void SubAllocatedBuffer<Placement>::destroy() {
    if (virtualBlock_) {
        virtualBlock_.clearVirtualBlock();
        virtualBlock_.destroy();
        virtualBlock_ = nullptr;
    }
    Base::destroy();
}

template <typename Placement>
SubAllocationHandle SubAllocatedBuffer<Placement>::allocate(vk::DeviceSize bytes,
                                                      vk::DeviceSize alignment) {
    if (!virtualBlock_ || bytes == 0) return {};

    vma::VirtualAllocationCreateInfo allocationInfo{};
    allocationInfo.size      = bytes;
    allocationInfo.alignment = alignment;

    vma::VirtualAllocation handle = nullptr;
    vk::DeviceSize         offset = 0;
    if (virtualBlock_.virtualAllocate(&allocationInfo, &handle, &offset) !=
        vk::Result::eSuccess)
        return {};

    return SubAllocationHandle{
        Base::region(offset, bytes),
        Base::template gpuAddress<std::byte>(offset),
        Base::mapped() ? Base::mapped() + offset : nullptr,
        handle};
}
