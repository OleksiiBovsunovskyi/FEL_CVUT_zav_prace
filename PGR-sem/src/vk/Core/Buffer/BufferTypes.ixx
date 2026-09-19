module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <cstddef>
#include <cstdint>

export module BufferTypes;

import GPUTypes;

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
 * A range the CPU may write.
 * `host` is non-null whenever the span itself is.
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

/// Read as storage through a device address, and filled by a transfer.
export constexpr vk::BufferUsageFlags GPU_DATA_USAGE =
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eShaderDeviceAddress;
