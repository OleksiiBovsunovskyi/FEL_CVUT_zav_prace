module;
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

export module VK_Buffers;

import VulkanContext;
import GPUTypes;

/**
 * Static, rarely updated buffers.
 *
 * Each holds one record type and is carved into fixed-stride elements.
 */
export enum class StaticBufferKind : uint8_t {
    Vertices,
    Meshlets,
    MeshletVertexIndices,
    MeshletTriangleIndices,
    Meshes,
    Materials,
    Clusters,
    ClusterGroups,
    Count,
};

/**
 * Duplicated per frame-in-flight, so a frame still executing is never
 * overwritten. Allocated in bytes; freed only by resetFrame().
 */
export enum class FrameBufferKind : uint8_t {
    FrameConstants,
    Objects,
    Lights,
    DrawData,
    MeshTaskCommands,
    MeshTaskCommandCount,
    Count,
};

/**
 * One suballocation inside a mega-buffer.
 *
 * `offset` and `elementIndex` address the same range in different units:
 * offset == elementIndex * stride. Vulkan copy and barrier calls take the byte
 * form, shaders take the element form.
 */
export struct BufferSlice {
    VkBuffer     buffer = VK_NULL_HANDLE;
    /// Byte offset into `buffer`.
    VkDeviceSize offset = 0;
    /// Size in bytes.
    VkDeviceSize size   = 0;
    /// Index of the first record. Only meaningful on a strided buffer.
    uint32_t     elementIndex = 0;

    /// Address of `offset`, or 0 when the buffer has no device address.
    VkDeviceAddress deviceAddress = 0;
    /// Host pointer to `offset`, or null when the buffer is not mapped.
    void*           mapped        = nullptr;

    /**
     * `deviceAddress` tagged with the record the range holds. Naming the type
     * here is what stops one buffer's address reaching another's shader field.
     */
    template <typename T>
    [[nodiscard]] GpuPtr<T> deviceAddressAs() const { return GpuPtr<T>{deviceAddress}; }

    /// @return true when this slice refers to a real range.
    [[nodiscard]] explicit operator bool() const {
        return buffer != VK_NULL_HANDLE && size != 0;
    }
};

export class VK_buffers;

/// Move-only ownership of a persistent static-buffer range.
export class BufferAllocation {
public:
    BufferAllocation() = default;
    BufferAllocation(const BufferAllocation&) = delete;
    BufferAllocation& operator=(const BufferAllocation&) = delete;
    BufferAllocation(BufferAllocation&& other) noexcept;
    BufferAllocation& operator=(BufferAllocation&& other) noexcept;
    ~BufferAllocation();

    /// @return the range owned, or a default slice once released or moved from.
    [[nodiscard]] const BufferSlice& slice() const { return slice_; }

    /// @return true while this owns a range.
    [[nodiscard]] explicit operator bool() const {
        return virtualAllocation_ != VK_NULL_HANDLE;
    }

private:
    friend class VK_buffers;

    BufferAllocation(BufferSlice slice, VK_buffers* owner,
                     VmaVirtualBlock block,
                     VmaVirtualAllocation allocation);
    void release();

    BufferSlice          slice_{};
    VK_buffers*          owner_             = nullptr;
    VmaVirtualBlock      block_             = VK_NULL_HANDLE;
    VmaVirtualAllocation virtualAllocation_ = VK_NULL_HANDLE;
};

/// Read-only snapshot of one whole mega-buffer.
export struct MegaBufferView {
    VkBuffer        buffer        = VK_NULL_HANDLE;
    /// Size in bytes.
    VkDeviceSize    capacity      = 0;
    /// Bytes currently allocated out of `capacity`.
    VkDeviceSize    used          = 0;
    /// Bytes per record, or 1 when allocated in raw bytes.
    VkDeviceSize    stride        = 1;
    VkDeviceAddress deviceAddress = 0;
    void*           mapped        = nullptr;

    /// See BufferSlice::deviceAddressAs.
    template <typename T>
    [[nodiscard]] GpuPtr<T> deviceAddressAs() const { return GpuPtr<T>{deviceAddress}; }
};

export constexpr size_t STATIC_BUFFER_COUNT =
    static_cast<size_t>(StaticBufferKind::Count);
export constexpr size_t FRAME_BUFFER_COUNT =
    static_cast<size_t>(FrameBufferKind::Count);

/// Read as storage through a device address, and filled by a transfer.
export constexpr VkBufferUsageFlags GPU_DATA_USAGE =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

/**
 * Static buffers take their stride from sizeof(Record), so the stride can no
 * longer disagree with the type the shader reads.
 */
export template <StaticBufferKind Kind> struct StaticBufferTraits;
export template <FrameBufferKind  Kind> struct FrameBufferTraits;

template <> struct StaticBufferTraits<StaticBufferKind::Vertices> {
    using Record = GPUVertex;
    static constexpr const char*  name     = "GPU vertices";
    static constexpr VkDeviceSize capacity = 64ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::Meshlets> {
    using Record = GPUMeshlet;
    static constexpr const char*  name     = "GPU meshlets";
    static constexpr VkDeviceSize capacity = 8ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::MeshletVertexIndices> {
    using Record = uint32_t;
    static constexpr const char*  name     = "GPU meshlet vertex indices";
    static constexpr VkDeviceSize capacity = 16ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::MeshletTriangleIndices> {
    using Record = uint32_t;
    static constexpr const char*  name     = "GPU meshlet triangle indices";
    static constexpr VkDeviceSize capacity = 16ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::Meshes> {
    using Record = GPUMesh;
    static constexpr const char*  name     = "GPU meshes";
    static constexpr VkDeviceSize capacity = 4ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::Materials> {
    using Record = GPUMaterial;
    static constexpr const char*  name     = "GPU materials";
    static constexpr VkDeviceSize capacity = 4ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::Clusters> {
    using Record = GPUCluster;
    static constexpr const char*  name     = "GPU CLOD clusters";
    static constexpr VkDeviceSize capacity = 8ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::ClusterGroups> {
    using Record = GPUClusterGroup;
    static constexpr const char*  name     = "GPU CLOD cluster groups";
    static constexpr VkDeviceSize capacity = 4ull << 20;
};

/// No Record yet: allocated and sized, but nothing declares what it holds.
template <> struct FrameBufferTraits<FrameBufferKind::FrameConstants> {
    static constexpr const char*        name         = "frame constants";
    static constexpr VkBufferUsageFlags usage        = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    static constexpr bool               hostWritable = true;
    static constexpr VkDeviceSize       capacity     = 64ull << 10;
};
template <> struct FrameBufferTraits<FrameBufferKind::Objects> {
    using Record = GPUObject;
    static constexpr const char*        name         = "objects";
    static constexpr VkBufferUsageFlags usage        = GPU_DATA_USAGE;
    static constexpr bool               hostWritable = true;
    static constexpr VkDeviceSize       capacity     = 8ull << 20;
};
/// No Record yet: waiting on a GPULight in Core/GPUTypes.ixx.
template <> struct FrameBufferTraits<FrameBufferKind::Lights> {
    static constexpr const char*        name         = "lights";
    static constexpr VkBufferUsageFlags usage        = GPU_DATA_USAGE;
    static constexpr bool               hostWritable = true;
    static constexpr VkDeviceSize       capacity     = 256ull << 10;
};
template <> struct FrameBufferTraits<FrameBufferKind::DrawData> {
    using Record = GPUDrawData;
    static constexpr const char*        name         = "draw data";
    static constexpr VkBufferUsageFlags usage        = GPU_DATA_USAGE;
    static constexpr bool               hostWritable = false;
    static constexpr VkDeviceSize       capacity     = 8ull << 20;
};
/// INDIRECT_BUFFER: read by vkCmdDrawMeshTasksIndirect*.
template <> struct FrameBufferTraits<FrameBufferKind::MeshTaskCommands> {
    using Record = GPUMeshTaskCommand;
    static constexpr const char*        name         = "mesh task commands";
    static constexpr VkBufferUsageFlags usage        = GPU_DATA_USAGE |
                                                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    static constexpr bool               hostWritable = false;
    static constexpr VkDeviceSize       capacity     = 4ull << 20;
};
/// TRANSFER_DST: zeroed by vkCmdFillBuffer before each dispatch.
template <> struct FrameBufferTraits<FrameBufferKind::MeshTaskCommandCount> {
    using Record = uint32_t;
    static constexpr const char*        name         = "mesh task command count";
    static constexpr VkBufferUsageFlags usage        = GPU_DATA_USAGE |
                                                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    static constexpr bool               hostWritable = false;
    static constexpr VkDeviceSize       capacity     = 4ull << 10;
};

/// Runtime form of the traits, for the loops that walk every buffer.
export struct StaticBufferSpec {
    VkDeviceSize stride;
    VkDeviceSize capacity;
    const char*  name;
};
export struct FrameBufferSpec {
    VkBufferUsageFlags usage;
    VkDeviceSize       capacity;
    const char*        name;
    /// Written by the CPU every frame, so it is mapped rather than device-local.
    bool               hostWritable;
};

namespace detail {

template <StaticBufferKind Kind>
constexpr StaticBufferSpec staticSpec() {
    using Traits = StaticBufferTraits<Kind>;
    return { sizeof(typename Traits::Record), Traits::capacity, Traits::name };
}

template <FrameBufferKind Kind>
constexpr FrameBufferSpec frameSpec() {
    using Traits = FrameBufferTraits<Kind>;
    return { Traits::usage, Traits::capacity, Traits::name, Traits::hostWritable };
}

/* Instantiating over the whole enum is what makes a gap a compile error. */
template <size_t... Index>
constexpr auto staticSpecs(std::index_sequence<Index...>) {
    return std::array<StaticBufferSpec, sizeof...(Index)>{
        staticSpec<static_cast<StaticBufferKind>(Index)>()... };
}

template <size_t... Index>
constexpr auto frameSpecs(std::index_sequence<Index...>) {
    return std::array<FrameBufferSpec, sizeof...(Index)>{
        frameSpec<static_cast<FrameBufferKind>(Index)>()... };
}

} // namespace detail

export inline constexpr auto STATIC_BUFFER_SPECS =
    detail::staticSpecs(std::make_index_sequence<STATIC_BUFFER_COUNT>{});
export inline constexpr auto FRAME_BUFFER_SPECS =
    detail::frameSpecs(std::make_index_sequence<FRAME_BUFFER_COUNT>{});

namespace detail {

template <typename Spec, size_t Count>
constexpr std::array<VkDeviceSize, Count> capacitiesOf(
    const std::array<Spec, Count>& specs) {
    std::array<VkDeviceSize, Count> out{};
    for (size_t i = 0; i < Count; ++i) out[i] = specs[i].capacity;
    return out;
}

} // namespace detail

/**
 * Byte capacities, defaulted from the traits. Strided buffers round down to
 * whole elements; a zero opts the buffer out entirely.
 */
export struct GPUBufferCapacities {
    std::array<VkDeviceSize, STATIC_BUFFER_COUNT> staticBytes =
        detail::capacitiesOf(STATIC_BUFFER_SPECS);
    std::array<VkDeviceSize, FRAME_BUFFER_COUNT> frameBytes =
        detail::capacitiesOf(FRAME_BUFFER_SPECS);

    VkDeviceSize upload         = 32ull << 20;
    uint32_t     framesInFlight = 2;
};

/**
 * Owns the large buffers used by the renderer.
 *
 * Static ranges are individually owned and reused after deferred retirement.
 * resetFrame() requires that frame slot's fence to have signaled;
 * resetUpload() requires every copy reading from staging to have completed.
 */
export class VK_buffers {
public:
    VK_buffers() = default;

    /// Logs if shutdown() was skipped. Teardown needs the device still alive.
    ~VK_buffers();

    VK_buffers(const VK_buffers&)            = delete;
    VK_buffers& operator=(const VK_buffers&) = delete;

    /**
     * Creates every mega-buffer at the default capacities.
     *
     * @param ctx supplies the device and VMA allocator; must outlive this.
     * @return false if already initialized or any buffer could not be created.
     */
    bool init(VulkanContext& ctx);

    /**
     * Creates every mega-buffer. A kind whose capacity is 0 is skipped, and
     * allocating from it then fails.
     *
     * @param ctx supplies the device and VMA allocator; must outlive this.
     * @param capacities byte capacity per buffer, plus the frame-in-flight count.
     * @return false if already initialized or any buffer could not be created.
     */
    bool init(VulkanContext& ctx, const GPUBufferCapacities& capacities);

    /// Destroys every mega-buffer. Requires the device to still be alive.
    void shutdown();

    /// @return true between a successful init() and shutdown().
    [[nodiscard]] bool initialized() const { return allocator_ != nullptr; }

    /// @return the frame-in-flight count init() was given.
    [[nodiscard]] uint32_t frameCount() const {
        return static_cast<uint32_t>(frameBuffers_.size());
    }

    /**
     * @param kind static buffer to query.
     * @return bytes per record, or 1 for StaticBufferKind::Count.
     */
    [[nodiscard]] static VkDeviceSize staticStride(StaticBufferKind kind);

    /**
     * Reserves consecutive records. The stride is the alignment, so the returned
     * elementIndex is exact.
     *
     * @param kind static buffer to allocate from.
     * @param elementCount records to reserve.
     * @return an owning allocation, or an empty one when the buffer is full.
     */
    [[nodiscard]] BufferAllocation allocateStatic(
        StaticBufferKind kind, uint32_t elementCount);

    /**
     * Reserves scratch space for one frame. Released only by resetFrame().
     *
     * @param frameIndex frame slot, below frameCount().
     * @param kind frame buffer to allocate from.
     * @param bytes size to reserve, in bytes.
     * @param alignment in bytes; must be a power of two.
     * @return an empty slice on a bad argument or when the buffer is full.
     */
    [[nodiscard]] BufferSlice allocateFrame(
        uint32_t frameIndex, FrameBufferKind kind,
        VkDeviceSize bytes, VkDeviceSize alignment = 16);

    /**
     * Reserves host-visible staging space. Valid until the next resetUpload().
     *
     * @param bytes size to reserve, in bytes.
     * @param alignment in bytes; must be a power of two.
     * @return a mapped slice, or an empty one when staging is full or `bytes`
     *         exceeds uploadCapacity().
     */
    [[nodiscard]] BufferSlice allocateUpload(
        VkDeviceSize bytes, VkDeviceSize alignment = 16);

    /**
     * @param kind static buffer to inspect.
     * @return its handle, capacity, bytes in use, stride and device address.
     */
    [[nodiscard]] MegaBufferView staticBuffer(StaticBufferKind kind) const;

    /**
     * @param frameIndex frame slot, below frameCount().
     * @param kind frame buffer to inspect.
     * @return an empty view when either index is out of range.
     */
    [[nodiscard]] MegaBufferView frameBuffer(
        uint32_t frameIndex, FrameBufferKind kind) const;

    /// @return a view of the staging buffer.
    [[nodiscard]] MegaBufferView uploadBuffer() const;

    /**
     * @return total staging bytes. A larger single request can never be
     *         satisfied.
     */
    [[nodiscard]] VkDeviceSize uploadCapacity() const { return upload_.capacity; }

    /**
     * Releases every slice in one frame slot. Requires that slot's fence to have
     * signaled.
     *
     * @param frameIndex frame slot, below frameCount().
     */
    void resetFrame(uint32_t frameIndex);

    /**
     * Releases every staging slice. Legal only once all copies reading from it
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

private:
    friend class BufferAllocation;

    struct MegaBuffer {
        VkBuffer        buffer        = VK_NULL_HANDLE;
        VmaAllocation   allocation    = nullptr;
        VmaVirtualBlock virtualBlock  = VK_NULL_HANDLE;
        VkDeviceSize    capacity      = 0;
        /// Bytes per element. 1 makes the units above bytes.
        VkDeviceSize    stride        = 1;
        VkDeviceAddress deviceAddress = 0;
        void*           mapped        = nullptr;
    };

    struct RawAllocation {
        BufferSlice          slice{};
        VmaVirtualBlock      block             = VK_NULL_HANDLE;
        VmaVirtualAllocation virtualAllocation = VK_NULL_HANDLE;
    };

    struct RetiredAllocation {
        VmaVirtualBlock      block             = VK_NULL_HANDLE;
        VmaVirtualAllocation virtualAllocation = VK_NULL_HANDLE;
        uint64_t             serial            = 0;
    };

    static constexpr size_t STATIC_COUNT =
        static_cast<size_t>(StaticBufferKind::Count);
    static constexpr size_t FRAME_COUNT =
        static_cast<size_t>(FrameBufferKind::Count);

    using StaticBuffers = std::array<MegaBuffer, STATIC_COUNT>;
    using FrameBuffers  = std::array<MegaBuffer, FRAME_COUNT>;

    VmaAllocator allocator_ = nullptr;
    VkDevice     device_    = VK_NULL_HANDLE;

    StaticBuffers             staticBuffers_{};
    std::vector<FrameBuffers> frameBuffers_;
    MegaBuffer                upload_{};
    std::vector<RetiredAllocation> retired_;
    uint64_t retirementSerial_ = 0;

    bool createBuffer(MegaBuffer& out, VkDeviceSize capacity, VkDeviceSize stride,
                      VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                      VmaAllocationCreateFlags allocationFlags,
                      const char* debugName);
    void destroyBuffer(MegaBuffer& buffer);

    void retire(VmaVirtualBlock block, VmaVirtualAllocation allocation);

    /**
     * @param units,alignment in stride units: bytes when stride is 1, elements
     *        otherwise.
     */
    [[nodiscard]] static RawAllocation allocate(
        MegaBuffer& buffer, VkDeviceSize units, VkDeviceSize alignment);
    [[nodiscard]] static MegaBufferView view(const MegaBuffer& buffer);
};
