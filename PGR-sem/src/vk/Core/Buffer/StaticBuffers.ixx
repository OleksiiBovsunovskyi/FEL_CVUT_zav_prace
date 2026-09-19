module;
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <cstddef>
#include <cstdint>

export module StaticBuffers;

import GPUTypes;
import BufferTypes;

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


export constexpr size_t STATIC_BUFFER_COUNT =
    static_cast<size_t>(StaticBufferKind::Count);

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
