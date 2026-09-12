module;

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include <glm/glm.hpp>

export module MeshDrawResources;

import BuildDrawCommands;
import Frame;
import FrameInFlightIndex;
import GPUTypes;
import VK_Buffers;
import VulkanContext;
import ShadersLoader;

/**
 * Mesh-draw buffers prepared for one Frame::Recording.
 * The buffer ranges remain valid until that recording's slot is reused.
 */
export struct PreparedMeshDraw {
    GpuPtr<GPUMeshInstance> instances{};
    GpuPtr<GPUDrawData>     drawData{};
    BufferRegion            indirectCommands{};
    BufferRegion            indirectCount{};
    uint32_t                instanceCount = 0;

    [[nodiscard]] explicit operator bool() const {
        return instances.address != 0 && drawData.address != 0 &&
               indirectCommands && indirectCount && instanceCount != 0;
    }
};

namespace detail {

class AllocatedBuffer {
public:
    [[nodiscard]] bool init(VulkanContext& ctx, vk::DeviceSize capacity,
                            vk::BufferUsageFlags usage, vma::MemoryUsage memory,
                            vma::AllocationCreateFlags flags, bool warnIfHost,
                            const char* name);
    void destroy();

    [[nodiscard]] vk::DeviceSize capacity() const { return capacity_; }
    [[nodiscard]] BufferRegion region(vk::DeviceSize size) const;

    template <typename T>
    [[nodiscard]] GpuPtr<T> gpuAddress() const {
        return GpuPtr<T>{deviceAddress_.address};
    }

    [[nodiscard]] std::byte* mapped() const { return mapped_; }

private:
    vma::Allocator  allocator_  = nullptr;
    vk::Buffer      buffer_     = nullptr;
    vma::Allocation allocation_ = nullptr;
    vk::DeviceSize  capacity_   = 0;
    GpuPtr<std::byte> deviceAddress_{};
    std::byte*       mapped_ = nullptr;
};

class MeshInstancesBuffer {
public:
    [[nodiscard]] bool init(VulkanContext& ctx);
    void destroy();
    [[nodiscard]] MappedSpan<GPUMeshInstance> span(uint32_t instanceCount) const;

private:
    AllocatedBuffer     buffer_;
    GPUMeshInstance*    mapped_ = nullptr;
};

class DrawDataBuffer {
public:
    [[nodiscard]] bool init(VulkanContext& ctx);
    void destroy();
    [[nodiscard]] DeviceSpan<GPUDrawData> span(uint32_t instanceCount) const;

private:
    AllocatedBuffer buffer_;
};

class MeshTaskCommandsBuffer {
public:
    [[nodiscard]] bool init(VulkanContext& ctx);
    void destroy();
    [[nodiscard]] DeviceSpan<GPUMeshTaskCommand> span(uint32_t instanceCount) const;

private:
    AllocatedBuffer buffer_;
};

class MeshTaskCommandCountBuffer {
public:
    [[nodiscard]] bool init(VulkanContext& ctx);
    void destroy();
    [[nodiscard]] DeviceSpan<uint32_t> span() const;

private:
    AllocatedBuffer buffer_;
};

struct MeshDrawResourceSlot {
    MeshInstancesBuffer        instances;
    DrawDataBuffer             drawData;
    MeshTaskCommandsBuffer     commands;
    MeshTaskCommandCountBuffer count;

    [[nodiscard]] bool init(VulkanContext& ctx);
    void destroy();
};

} // namespace detail

/**
 * Owns the shared mesh-draw producer resources for every frame-in-flight slot.
 * BuildDrawCommands writes indirect commands consumed by renderers.
 */
export class MeshDrawResources {
public:
    MeshDrawResources() = default;
    ~MeshDrawResources() { destroy(); }

    MeshDrawResources(const MeshDrawResources&)            = delete;
    MeshDrawResources& operator=(const MeshDrawResources&) = delete;

    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            const std::filesystem::path& shaderPath);
    void destroy();

    /**
     * Writes instances, records BuildDrawCommands, and publishes its output for drawing.
     * @param recording active frame recording.
     * @param instances visible mesh instances.
     * @param viewProjection world-to-clip matrix for BuildDrawCommands.
     * @return prepared indirect draw resources, or empty when no draw can be recorded.
     */
    [[nodiscard]] PreparedMeshDraw prepare(
        Frame::Recording& recording, std::span<const GPUMeshInstance> instances,
        const glm::mat4& viewProjection);

private:
    std::array<detail::MeshDrawResourceSlot, FRAMES_IN_FLIGHT> slots_;
    BuildDrawCommands buildDrawCommands_;
};
