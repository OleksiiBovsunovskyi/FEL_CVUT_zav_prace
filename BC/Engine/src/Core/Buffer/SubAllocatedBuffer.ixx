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
export import RangeAllocator;

/**
 * Identifies one range reserved inside a SubAllocatedBuffer.
 * The range is not owned; SubAllocatedBuffer::free() releases it.
 * `host` is non-null only for a host-writable placement.
 */
export struct SubAllocationHandle {
    BufferRegion      region{};
    GpuPtr<std::byte> address{};
    std::byte*        host = nullptr;
    RangeAllocation   range{};

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(range); }
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
    ~SubAllocatedBuffer() override { this->destroy(); }

    SubAllocatedBuffer(const SubAllocatedBuffer&)            = delete;
    SubAllocatedBuffer& operator=(const SubAllocatedBuffer&) = delete;
    SubAllocatedBuffer& operator=(SubAllocatedBuffer&& other) =delete;


    SubAllocatedBuffer(SubAllocatedBuffer&& other) noexcept
        : Base(std::move(other)), ranges_(std::move(other.ranges_)) {}

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
    void free(const SubAllocationHandle& allocation) { ranges_.free(allocation.range); }

    /**
     * Releases every range at once.
     * @note The GPU must be done with every range before this is called.
     */
    void clear() { ranges_.clear(); }

    /// @return bytes currently reserved.
    [[nodiscard]] vk::DeviceSize used() const { return ranges_.used(); }

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

protected:
    /// Releases every range and destroys the buffer.
    void release() override;

private:
    /// Units are bytes.
    RangeAllocator ranges_;
};

/* ------------------------------------------------------------------------ */

template <typename Placement>
bool SubAllocatedBuffer<Placement>::init(VulkanContext& ctx,
                                         vk::DeviceSize capacity,
                                         vk::BufferUsageFlags usage,
                                         const char* debugName) {
    if (!Base::init(ctx, capacity, usage, debugName)) return false;

    if (!ranges_.init(capacity)) {
        logError(std::string("SubAllocatedBuffer: vmaCreateVirtualBlock failed for ") +
                 (debugName ? debugName : "buffer"));
        this->destroy();
        return false;
    }
    return true;
}

template <typename Placement>
void SubAllocatedBuffer<Placement>::release() {
    ranges_.destroy();
    Base::release();
}

template <typename Placement>
SubAllocationHandle SubAllocatedBuffer<Placement>::allocate(vk::DeviceSize bytes,
                                                      vk::DeviceSize alignment) {
    const RangeAllocation range = ranges_.allocate(bytes, alignment);
    if (!range) return {};

    return SubAllocationHandle{
        Base::region(range.offset, bytes),
        Base::template gpuAddress<std::byte>(range.offset),
        Base::mapped() ? Base::mapped() + range.offset : nullptr,
        range};
}
