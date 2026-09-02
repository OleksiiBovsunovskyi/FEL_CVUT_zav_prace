module;
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

export module VK_Buffers;

import VulkanContext;
import GPUTypes;

/**
 * Static buffers, written once per asset and read every frame.
 *
 * MeshData is byte-addressed: one blob per mesh, a GPUMeshHeader first, every
 * other section reached through a pointer in that header.
 */
export enum class StaticBufferKind : uint8_t {
    MeshData,
    Materials,
    Count,
};

/**
 * Duplicated per frame-in-flight, so a frame still executing is never
 * overwritten. Freed only by resetFrame().
 */
export enum class FrameSlotBufferKind : uint8_t {
    MeshInstances,
    DrawData,
    MeshTaskCommands,
    MeshTaskCommandCount,
    Count,
};

/**
 * Byte range of a VkBuffer: what vkCmdCopyBuffer, vkCmdFillBuffer, buffer
 * barriers and the indirect draw take. Carries no address and no host pointer,
 * so it cannot stand in for either.
 */
export struct BufferRegion {
    VkBuffer     buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size   = 0;

    [[nodiscard]] explicit operator bool() const {
        return buffer != VK_NULL_HANDLE && size != 0;
    }
};

/**
 * A range the CPU cannot write: `region` addresses it for commands, `gpu`
 * addresses it for shaders.
 */
export template <typename T>
struct DeviceSpan {
    BufferRegion region{};
    GpuSpan<T>   gpu{};

    [[nodiscard]] explicit operator bool() const { return bool(region); }
};

/**
 * A range the CPU may write. `host` is non-null whenever the span itself is
 */
export template <typename T>
struct MappedSpan : DeviceSpan<T> {
    T* host = nullptr;
};

/* ------------------------------------------------------------------------ */
/* Placement: one type per memory policy, each carrying its own VMA flags and
 * the span type an allocation out of it produces. */

/// VRAM. The CPU reaches it only through a copy.
export struct DeviceOnlyBuffer {
    template <typename T> using Span = DeviceSpan<T>;

    static constexpr VmaMemoryUsage           memory       = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    static constexpr VmaAllocationCreateFlags flags        = 0;
    static constexpr bool                     hostWritable = false;
    /// Device-local is the only acceptable placement, so no warning is possible.
    static constexpr bool                     warnIfHost   = false;
};

/// VRAM the CPU writes through the BAR window; VMA falls back to host memory.
export struct DeviceHostMappedBuffer {
    template <typename T> using Span = MappedSpan<T>;

    static constexpr VmaMemoryUsage           memory =
        VMA_MEMORY_USAGE_AUTO;
    static constexpr VmaAllocationCreateFlags flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;
    static constexpr bool                     hostWritable = true;
    /// The fallback to host memory is silent otherwise.
    static constexpr bool                     warnIfHost   = true;
};

/// Host memory the GPU reads over PCIe.
export struct HostDeviceReadableBuffer {
    template <typename T> using Span = MappedSpan<T>;

    static constexpr VmaMemoryUsage           memory =
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    static constexpr VmaAllocationCreateFlags flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;
    static constexpr bool                     hostWritable = true;
    static constexpr bool                     warnIfHost   = false;
};

export constexpr size_t STATIC_BUFFER_COUNT =
    static_cast<size_t>(StaticBufferKind::Count);
export constexpr size_t FRAME_SLOT_BUFFER_COUNT =
    static_cast<size_t>(FrameSlotBufferKind::Count);

/// Read as storage through a device address, and filled by a transfer.
export constexpr VkBufferUsageFlags GPU_DATA_USAGE =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

/**
 * One specialization per kind. `Record` is the type an allocation counts in,
 * `Buffer` is the placement, and both drive the compile-time checks below.
 */
export template <StaticBufferKind Kind> struct StaticBufferTraits;
export template <FrameSlotBufferKind  Kind> struct FrameSlotBufferTraits;

template <> struct StaticBufferTraits<StaticBufferKind::MeshData> {
    /// Heterogeneous: the GPUMeshHeader at the front carries the section pointers.
    using Record = std::byte;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*        name     = "mesh data";
    static constexpr VkBufferUsageFlags usage    = GPU_DATA_USAGE;
    static constexpr VkDeviceSize       capacity = 128ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::Materials> {
    using Record = GPUMaterial;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*        name     = "materials";
    static constexpr VkBufferUsageFlags usage    = GPU_DATA_USAGE;
    static constexpr VkDeviceSize       capacity = 4ull << 20;
};

template <> struct FrameSlotBufferTraits<FrameSlotBufferKind::MeshInstances> {
    using Record = GPUMeshInstance;
    using Buffer = DeviceHostMappedBuffer;
    static constexpr const char*        name     = "mesh instances";
    static constexpr VkBufferUsageFlags usage    = GPU_DATA_USAGE;
    static constexpr VkDeviceSize       capacity = 8ull << 20;
};
template <> struct FrameSlotBufferTraits<FrameSlotBufferKind::DrawData> {
    using Record = GPUDrawData;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*        name     = "draw data";
    static constexpr VkBufferUsageFlags usage    = GPU_DATA_USAGE;
    static constexpr VkDeviceSize       capacity = 2ull << 20;
};
/// INDIRECT_BUFFER: read by vkCmdDrawMeshTasksIndirect*.
template <> struct FrameSlotBufferTraits<FrameSlotBufferKind::MeshTaskCommands> {
    using Record = GPUMeshTaskCommand;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*        name     = "mesh task commands";
    static constexpr VkBufferUsageFlags usage    = GPU_DATA_USAGE |
                                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    static constexpr VkDeviceSize       capacity = 2ull << 20;
};
/// TRANSFER_DST: zeroed by vkCmdFillBuffer before each dispatch.
template <> struct FrameSlotBufferTraits<FrameSlotBufferKind::MeshTaskCommandCount> {
    using Record = uint32_t;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*        name     = "mesh task command count";
    static constexpr VkBufferUsageFlags usage    = GPU_DATA_USAGE |
                                                   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    static constexpr VkDeviceSize       capacity = 4ull << 10;
};

/// The record a kind holds, so call sites name a kind and get a typed span.
export template <StaticBufferKind Kind>
using StaticRecord = typename StaticBufferTraits<Kind>::Record;
export template <FrameSlotBufferKind Kind>
using FrameSlotRecord = typename FrameSlotBufferTraits<Kind>::Record;

/// DeviceSpan or MappedSpan, whichever the kind's placement produces.
export template <FrameSlotBufferKind Kind>
using FrameSlotSpan =
    typename FrameSlotBufferTraits<Kind>::Buffer::template Span<FrameSlotRecord<Kind>>;

namespace detail {

/* A mapped buffer the shaders never see, or an unmapped one the CPU is asked
 * to write, is a contradiction the traits can catch here. */
template <FrameSlotBufferKind Kind>
constexpr bool frameTraitsConsistent() {
    using Traits = FrameSlotBufferTraits<Kind>;
    return !Traits::Buffer::hostWritable ||
           (Traits::usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0 ||
           (Traits::usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
}

} // namespace detail

static_assert(detail::frameTraitsConsistent<FrameSlotBufferKind::MeshInstances>());

