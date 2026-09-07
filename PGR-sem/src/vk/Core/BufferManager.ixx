module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

export module BufferManager;

import VulkanContext;
import GPUTypes;
export import VK_Buffers;

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
        return virtualAllocation_ != nullptr;
    }

private:
    friend class BufferManager;

    DeviceArray(DeviceSpan<T> span, BufferManager* owner, vma::VirtualBlock block,
                vma::VirtualAllocation allocation)
        : span_(span), owner_(owner), block_(block),
          virtualAllocation_(allocation) {}

    /* Defined out of line: BufferManager::retire is not declared yet. */
    void release();

    void swap(DeviceArray& other) noexcept {
        std::swap(span_, other.span_);
        std::swap(owner_, other.owner_);
        std::swap(block_, other.block_);
        std::swap(virtualAllocation_, other.virtualAllocation_);
    }

    DeviceSpan<T>          span_{};
    BufferManager*         owner_             = nullptr;
    vma::VirtualBlock      block_             = nullptr;
    vma::VirtualAllocation virtualAllocation_ = nullptr;
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
    std::array<vk::DeviceSize, FRAME_SLOT_BUFFER_COUNT> frameBytes{
        FrameSlotBufferTraits<FrameSlotBufferKind::MeshInstances>::capacity,
        FrameSlotBufferTraits<FrameSlotBufferKind::DrawData>::capacity,
        FrameSlotBufferTraits<FrameSlotBufferKind::MeshTaskCommands>::capacity,
        FrameSlotBufferTraits<FrameSlotBufferKind::MeshTaskCommandCount>::capacity,
    };

    vk::DeviceSize upload         = 32ull << 20;
    uint32_t       framesInFlight = 2;
};

/**
 * Owns every buffer the renderer uses, and suballocates ranges inside them.
 * Static ranges are individually owned and reused after deferred retirement; frame ranges are
 * released wholesale by resetFrame() once that slot's fence has signalled.
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

    [[nodiscard]] bool initialized() const { return allocator_ != nullptr; }

    [[nodiscard]] uint32_t frameSlotCount() const {
        return static_cast<uint32_t>(frameSlotBuffers_.size());
    }

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

        const RawAllocation raw = allocateStaticRaw(
            Kind, vk::DeviceSize{count} * sizeof(Record), alignment);
        if (!raw.region) return {};

        return DeviceArray<Record>{
            spanOf<DeviceOnlyBuffer, Record>(raw, count),
            this, raw.block, raw.virtualAllocation};
    }

    /**
     * Reserves scratch space in one frame slot. Released only by resetFrame().
     *
     * @param frameIndex frame slot, below frameSlotCount().
     * @param count records to reserve.
     * @param alignment in bytes; must be a power of two.
     * @return an empty span on a bad argument or when the buffer is full.
     */
    template <FrameSlotBufferKind Kind>
    [[nodiscard]] FrameSlotSpan<Kind> allocateFrame(
        uint32_t frameIndex, uint32_t count,
        vk::DeviceSize alignment = alignof(FrameSlotRecord<Kind>)) {
        using Record = FrameSlotRecord<Kind>;
        using Buffer = typename FrameSlotBufferTraits<Kind>::Buffer;

        const RawAllocation raw = allocateFrameRaw(
            frameIndex, Kind, vk::DeviceSize{count} * sizeof(Record), alignment);
        if (!raw.region) return {};

        return spanOf<Buffer, Record>(raw, count);
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
        return GpuPtr<StaticRecord<Kind>>{staticBaseAddress(Kind)};
    }

    /// @return name, capacity and bytes in use. Debug reporting only.
    [[nodiscard]] BufferUsage staticUsage(StaticBufferKind kind) const;

    /// @return total upload bytes. A larger single request can never be met.
    [[nodiscard]] vk::DeviceSize uploadCapacity() const { return upload_.capacity; }

    /**
     * Releases every range in one frame slot. Requires that slot's fence to
     * have signalled.
     *
     * @param frameIndex frame slot, below frameSlotCount().
     */
    void resetFrame(uint32_t frameIndex);

    /**
     * Releases every upload range. Legal only once all copies reading from it
     * have completed; UploadBatch guarantees that.
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
    void retire(vma::VirtualBlock block, vma::VirtualAllocation allocation);

private:
    struct MegaBuffer {
        vk::Buffer        buffer        = nullptr;
        vma::Allocation   allocation    = nullptr;
        vma::VirtualBlock virtualBlock  = nullptr;
        vk::DeviceSize    capacity      = 0;
        vk::DeviceAddress deviceAddress = 0;
        void*             mapped        = nullptr;
    };

    /* Byte-addressed in every buffer: nothing indexes a mega-buffer by record
     * any more, so the virtual blocks count bytes and strides are gone. */
    struct RawAllocation {
        BufferRegion           region{};
        vk::DeviceAddress      address           = 0;
        void*                  host              = nullptr;
        vma::VirtualBlock      block             = nullptr;
        vma::VirtualAllocation virtualAllocation = nullptr;
    };

    struct RetiredAllocation {
        vma::VirtualBlock      block             = nullptr;
        vma::VirtualAllocation virtualAllocation = nullptr;
        uint64_t               serial            = 0;
    };

    using StaticBuffers = std::array<MegaBuffer, STATIC_BUFFER_COUNT>;
    using FrameBuffers  = std::array<MegaBuffer, FRAME_SLOT_BUFFER_COUNT>;

    /// Fills in whichever span type the placement declares.
    template <typename Buffer, typename T>
    static typename Buffer::template Span<T> spanOf(const RawAllocation& raw,
                                                    uint32_t count) {
        typename Buffer::template Span<T> span{};
        span.region = raw.region;
        span.gpu    = GpuSpan<T>{GpuPtr<T>{raw.address}, count};
        if constexpr (Buffer::hostWritable)
            span.host = static_cast<T*>(raw.host);
        return span;
    }

    [[nodiscard]] RawAllocation allocateStaticRaw(
        StaticBufferKind kind, vk::DeviceSize bytes, vk::DeviceSize alignment);
    [[nodiscard]] RawAllocation allocateFrameRaw(
        uint32_t frameIndex, FrameSlotBufferKind kind,
        vk::DeviceSize bytes, vk::DeviceSize alignment);
    [[nodiscard]] RawAllocation allocate(
        MegaBuffer& buffer, vk::DeviceSize bytes, vk::DeviceSize alignment);

    [[nodiscard]] vk::DeviceAddress staticBaseAddress(StaticBufferKind kind) const;

    bool createBuffer(MegaBuffer& out, vk::DeviceSize capacity,
                      vk::BufferUsageFlags usage, vma::MemoryUsage memory,
                      vma::AllocationCreateFlags flags, bool warnIfHost,
                      const char* debugName);
    void destroyBuffer(MegaBuffer& buffer);

    vma::Allocator allocator_ = nullptr;
    vk::Device     device_    = nullptr;

    StaticBuffers                  staticBuffers_{};
    std::vector<FrameBuffers>      frameSlotBuffers_;
    MegaBuffer                     upload_{};
    std::vector<RetiredAllocation> retired_;
    uint64_t                       retirementSerial_ = 0;
};

template <typename T>
void DeviceArray<T>::release() {
    if (owner_ && virtualAllocation_) owner_->retire(block_, virtualAllocation_);
    span_              = {};
    owner_             = nullptr;
    block_             = nullptr;
    virtualAllocation_ = nullptr;
}
