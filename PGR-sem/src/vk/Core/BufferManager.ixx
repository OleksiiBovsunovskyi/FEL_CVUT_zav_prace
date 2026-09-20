module;

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module BufferManager;

import vulkan;
import vk_mem_alloc;
import VulkanContext;
import GPUTypes;
export import VK_Buffers;
import Logger;

export class BufferManager;

/**
 * Move-only ownership of a range in a static buffer. Retires the range on
 * destruction, so it is reused once the GPU has passed the retirement serial.
 */
export template <typename T>
class DeviceArray {
public:
    DeviceArray() = default;
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;

    DeviceArray(DeviceArray&& other) noexcept { swap(other); }
    DeviceArray& operator=(DeviceArray&& other) noexcept {
        if (this != &other) {
            release();
            swap(other);
        }
        return *this;
    }
    ~DeviceArray() { release(); }

    /// @return the range owned, or an empty span once released or moved from.
    [[nodiscard]] const DeviceSpan<T>& span() const { return span_; }

    /// @return true while this owns a range.
    [[nodiscard]] explicit operator bool() const {
        return bool(allocation_);
    }

private:
    friend class BufferManager;

    DeviceArray(DeviceSpan<T> span, BufferManager* owner, StaticBufferKind kind,
                const SubAllocationHandle& allocation)
        : span_(span), owner_(owner), kind_(kind), allocation_(allocation) {}

    /* Defined out of line: BufferManager::retire is not declared yet. */
    void release();

    void swap(DeviceArray& other) noexcept {
        std::swap(span_, other.span_);
        std::swap(owner_, other.owner_);
        std::swap(kind_, other.kind_);
        std::swap(allocation_, other.allocation_);
    }

    DeviceSpan<T>       span_{};
    BufferManager*      owner_ = nullptr;
    StaticBufferKind    kind_  = StaticBufferKind::Count;
    SubAllocationHandle allocation_{};
};

/// Bytes in use out of a buffer's capacity. Debug reporting only.
export struct BufferUsage {
    const char*    name     = "";
    vk::DeviceSize capacity = 0;
    vk::DeviceSize used     = 0;
};

/**
 * Byte capacities, defaulted from the traits. A zero opts the buffer out.
 */
export struct GPUBufferCapacities {
    std::array<vk::DeviceSize, STATIC_BUFFER_COUNT> staticBytes{
        StaticBufferTraits<StaticBufferKind::MeshData>::capacity,
        StaticBufferTraits<StaticBufferKind::Materials>::capacity,
    };
    vk::DeviceSize upload = 256ull << 20;
};

/**
 * Owns the static mega-buffers, static range retirement, and the upload buffer.
 */
export class BufferManager {
public:
    BufferManager() = default;

    /// Logs if shutdown() was skipped. Teardown needs the device still alive.
    ~BufferManager();

    BufferManager(const BufferManager&)            = delete;
    BufferManager& operator=(const BufferManager&) = delete;

    /**
     * Creates every buffer at the given capacities.
     *
     * @param ctx supplies the device and VMA allocator; must outlive this.
     * @param capacities byte sizes and the frame-in-flight count.
     * @return false if called twice, or if any buffer could not be created.
     */
    bool init(VulkanContext& ctx, const GPUBufferCapacities& capacities = {});

    /// Destroys every buffer. Safe to call twice.
    void shutdown();

    /**
     * Checks whether this buffer manager have been initialized.
     * @return true after init() and before shutdown().
     */
    [[nodiscard]] bool initialized() const { return initialized_; }

    /**
     * Reserves a range in a static buffer for as long as the returned array
     * lives.
     *
     * @param count records to reserve; bytes for a byte-addressed buffer.
     * @param alignment in bytes; must be a power of two.
     * @return an owning array, or an empty one when the buffer is full.
     */
    template <StaticBufferKind Kind>
    [[nodiscard]] DeviceArray<StaticRecord<Kind>> allocateStatic(
        uint32_t count, vk::DeviceSize alignment = alignof(StaticRecord<Kind>)) {
        using Record = StaticRecord<Kind>;

        if (!std::has_single_bit(alignment))
        {
            logError("BufferManager: allocateStatic alignment must be a power of two, got " + std::to_string(alignment));
            return DeviceArray<Record>{};
        }

        const SubAllocationHandle allocation = allocateStaticRaw(
            Kind, vk::DeviceSize{count} * sizeof(Record), alignment);
        if (!allocation) return DeviceArray<Record>{};

        return DeviceArray<Record>{
            StaticBuffer::spanOf<Record>(allocation, count), this, Kind,
            allocation};
    }

    /**
     * Reserves upload space. Valid until the next resetUpload().
     *
     * @param bytes size to reserve.
     * @param alignment in bytes; must be a power of two.
     * @return an empty span when the buffer is full or `bytes` exceeds
     *         uploadCapacity().
     */
    [[nodiscard]] MappedSpan<std::byte> allocateUpload(
        vk::DeviceSize bytes, vk::DeviceSize alignment = 16);

    /**
     * @return the address of a whole static buffer, for the shaders that index
     *         it rather than being handed a range.
     */
    template <StaticBufferKind Kind>
    [[nodiscard]] GpuPtr<StaticRecord<Kind>> staticBase() const {
        return GpuPtr<StaticRecord<Kind>>{staticBaseAddress(Kind).address};
    }

    /// @return name, capacity and bytes in use. Debug reporting only.
    [[nodiscard]] BufferUsage staticUsage(StaticBufferKind kind) const;

    /// @return total upload bytes. A larger single request can never be met.
    [[nodiscard]] vk::DeviceSize uploadCapacity() const { return upload_.capacity(); }

    /**
     * Releases every upload range.
     * @note Legal only once every copy reading from the upload buffer has
     *       completed.
     */
    void resetUpload();

    /**
     * Stamps subsequently retired ranges, so they are held until this serial
     * completes.
     *
     * @param serial newest submission that may reference static allocations.
     */
    void setRetirementSerial(uint64_t serial) { retirementSerial_ = serial; }

    /**
     * Reuses ranges whose last referencing submission has finished.
     *
     * @param completedSerial highest serial the GPU has completed.
     */
    void collect(uint64_t completedSerial);

    /// Called by DeviceArray on destruction. Not part of the allocation API.
    void retire(StaticBufferKind kind, const SubAllocationHandle& allocation);

private:
    using StaticBuffer = SubAllocatedBuffer<DeviceOnlyBuffer>;
    using UploadBuffer = SubAllocatedBuffer<HostDeviceReadableBuffer>;

    struct RetiredAllocation {
        StaticBufferKind    kind   = StaticBufferKind::Count;
        SubAllocationHandle allocation{};
        uint64_t            serial = 0;
    };

    [[nodiscard]] SubAllocationHandle allocateStaticRaw(
        StaticBufferKind kind, vk::DeviceSize bytes, vk::DeviceSize alignment);

    [[nodiscard]] GpuPtr<std::byte> staticBaseAddress(StaticBufferKind kind) const;

    std::array<StaticBuffer, STATIC_BUFFER_COUNT> staticBuffers_{};
    UploadBuffer                                  upload_{};
    std::vector<RetiredAllocation>                retired_;
    uint64_t                                      retirementSerial_ = 0;
    bool                                          initialized_      = false;
};

template <typename T>
void DeviceArray<T>::release() {
    if (owner_ && allocation_) owner_->retire(kind_, allocation_);
    span_       = {};
    owner_      = nullptr;
    kind_       = StaticBufferKind::Count;
    allocation_ = {};
}
