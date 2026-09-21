module;

#include <cstddef>
#include <cstdint>

export module StaticBuffers;

import vulkan;
import vk_mem_alloc;
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
 * One specialization per kind.
 * `Record` is the type an allocation counts in.
 * `Buffer` is the placement.
 * `blockBytes` is one VkBuffer; a growable kind appends another when full.
 * `growable` kinds span several blocks and have no single base address.
 */
export template <StaticBufferKind Kind> struct StaticBufferTraits;

template <> struct StaticBufferTraits<StaticBufferKind::MeshData> {
    /// Heterogeneous: the GPUMeshHeader at the front carries the section pointers.
    using Record = std::byte;
    using Buffer = DeviceOnlyBuffer;
    static constexpr const char*          name       = "mesh data";
    static constexpr vk::BufferUsageFlags usage      = GPU_DATA_USAGE;
    static constexpr vk::DeviceSize       blockBytes = 64ull << 20;
    static constexpr bool                 growable   = true;
};
template <> struct StaticBufferTraits<StaticBufferKind::Materials> {
    using Record = GPUMaterial;
    using Buffer = DeviceOnlyBuffer;
    /// Indexed by GPUMaterialIndex from one base address, so it cannot grow.
    static constexpr const char*          name       = "materials";
    static constexpr vk::BufferUsageFlags usage      = GPU_DATA_USAGE;
    static constexpr vk::DeviceSize       blockBytes = 4ull << 20;
    static constexpr bool                 growable   = false;
};

/// The record a static buffer kind holds.
export template <StaticBufferKind Kind>
using StaticRecord = typename StaticBufferTraits<Kind>::Record;
