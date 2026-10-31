module;

#include <vulkan/vulkan_core.h>

#include <cstdint>

export module GPUCoreTypes;
/**
 * C++ spellings of shader built-ins that shader-facing records are made of.
 */

/**
 * Device address of an array of T, and the C++ spelling of a `T*` in a shader.
 *
 * A plain VkDeviceAddress makes every buffer the same type, so swapping two
 * push constant fields compiles and reads garbage on the GPU. Naming the
 * pointee makes that a compile error. Obtained from a DeviceSpan or MappedSpan
 * handed out by BufferManager.
 */
export template <typename T>
struct GpuPtr {
    VkDeviceAddress address = 0;
};

static_assert(sizeof(GpuPtr<float>) == sizeof(VkDeviceAddress));


export struct GpuBool {
    uint32_t value = 0;

    constexpr GpuBool() = default;
    constexpr GpuBool(bool b) : value(b ? 1u : 0u) {}
    constexpr operator bool() const { return value != 0; }
};

static_assert(sizeof(GpuBool) == 4);

/**
 * Device address of a T[count]. The shader-facing form of a range; a bare
 * address plus a loose length is what it replaces.
 */
export template <typename T>
struct GpuSpan {
    GpuPtr<T> data{};
    uint32_t  count = 0;

    [[nodiscard]] explicit operator bool() const {
        return data.address != 0 && count != 0;
    }
};
