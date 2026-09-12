module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

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
 * Byte range of a vk::Buffer.
 */
export struct BufferRegion {
    vk::Buffer     buffer = nullptr;
    vk::DeviceSize offset = 0;
    vk::DeviceSize size   = 0;

    [[nodiscard]] explicit operator bool() const {
        return buffer && size != 0;
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

    static constexpr vma::MemoryUsage           memory       = vma::MemoryUsage::eAutoPreferDevice;
    static constexpr vma::AllocationCreateFlags flags        = {};
    static constexpr bool                       hostWritable = false;
    /// Device-local is the only acceptable placement, so no warning is possible.
    static constexpr bool                     warnIfHost   = false;
};

/// VRAM the CPU writes through the BAR window; VMA falls back to host memory.
export struct DeviceHostMappedBuffer {
    template <typename T> using Span = MappedSpan<T>;

    static constexpr vma::MemoryUsage           memory =
        vma::MemoryUsage::eAuto;
    static constexpr vma::AllocationCreateFlags flags =
        vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
        vma::AllocationCreateFlagBits::eMapped;
    static constexpr bool                       hostWritable = true;
    /// The fallback to host memory is silent otherwise.
    static constexpr bool                       warnIfHost   = true;
};

/// Host memory the GPU reads over PCIe.
export struct HostDeviceReadableBuffer {
    template <typename T> using Span = MappedSpan<T>;

    static constexpr vma::MemoryUsage           memory =
        vma::MemoryUsage::eAutoPreferHost;
    static constexpr vma::AllocationCreateFlags flags =
        vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
        vma::AllocationCreateFlagBits::eMapped;
    static constexpr bool                       hostWritable = true;
    static constexpr bool                       warnIfHost   = false;
};

export constexpr size_t STATIC_BUFFER_COUNT =
    static_cast<size_t>(StaticBufferKind::Count);

/// Read as storage through a device address, and filled by a transfer.
export constexpr vk::BufferUsageFlags GPU_DATA_USAGE =
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eShaderDeviceAddress;

/**
 * One specialization per kind. `Record` is the type an allocation counts in,
 * `Buffer` is the placement, and both drive the compile-time checks below.
 */
export template <StaticBufferKind Kind> struct StaticBufferTraits;

template <> struct StaticBufferTraits<StaticBufferKind::MeshData> {
    /// Heterogeneous: the GPUMeshHeader at the front carries the section pointers.
    using Record = std::byte;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*        name     = "mesh data";
    static constexpr vk::BufferUsageFlags usage    = GPU_DATA_USAGE;
    static constexpr vk::DeviceSize       capacity = 128ull << 20;
};
template <> struct StaticBufferTraits<StaticBufferKind::Materials> {
    using Record = GPUMaterial;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*        name     = "materials";
    static constexpr vk::BufferUsageFlags usage    = GPU_DATA_USAGE;
    static constexpr vk::DeviceSize       capacity = 4ull << 20;
};

/// The record a static buffer kind holds.
export template <StaticBufferKind Kind>
using StaticRecord = typename StaticBufferTraits<Kind>::Record;

