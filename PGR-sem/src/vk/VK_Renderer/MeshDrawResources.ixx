module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module MeshDrawResources;

import vulkan;
import vk_mem_alloc;
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

/**
 * One slot's buffers as spans, sized to an instance count.
 */
struct MeshDrawSpans {
    DeviceSpan<GPUMeshInstance>    instances{};
    MappedSpan<GPUMeshInstance>    upload{};
    DeviceSpan<GPUDrawData>        drawData{};
    DeviceSpan<GPUMeshTaskCommand> commands{};
    DeviceSpan<uint32_t>           count{};

    [[nodiscard]] explicit operator bool() const {
        return instances && upload && drawData && commands && count;
    }
};

/**
 * The mesh-draw buffers one frame-in-flight slot owns.
 * Instances are written into instanceUpload and copied into instances.
 * drawData, commands and count are written by BuildDrawCommands.
 */
struct MeshDrawResourceSlot {
    AllocatedBuffer<DeviceOnlyBuffer>         instances;
    AllocatedBuffer<HostDeviceReadableBuffer> instanceUpload;
    AllocatedBuffer<DeviceOnlyBuffer>         drawData;
    AllocatedBuffer<DeviceOnlyBuffer>         commands;
    AllocatedBuffer<DeviceOnlyBuffer>         count;

    /**
     * Instance indices pending upload into `instances`.
     */
    std::vector<uint32_t> indicesPendingUpload;

    [[nodiscard]] bool init(VulkanContext& ctx);
    void destroy();

    /**
     * @param instanceCount instances the recording draws.
     * @return the buffers as spans, empty when instanceCount exceeds what they
     *         hold.
     */
    [[nodiscard]] MeshDrawSpans spans(uint32_t instanceCount) const;
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
     * Uploads the changed instances, records BuildDrawCommands, and publishes
     * its output for drawing.
     * @param recording active frame recording.
     * @param instances every registered mesh instance, in DrawList order.
     * @param changed indices of `instances` written since the previous call;
     * @param viewProjection world-to-clip matrix for BuildDrawCommands.
     * @return prepared indirect draw resources, or empty when no draw can be recorded.
     * @note Must be called once per recording, including when `instances` is
     *       empty.
     */
    [[nodiscard]] PreparedMeshDraw prepare(
        Frame::Recording& recording, std::span<const GPUMeshInstance> instances,
        std::span<const uint32_t> changed, const glm::mat4& viewProjection);

private:
    /**
     * Copies the pending instances into `upload` and fills
     * MeshDrawResources::copyRegions_ with one entry per consecutive group.
     * @param indicesPendingUpload one slot's pending indices; emptied.
     * @param instances every registered mesh instance, in DrawList order.
     * @param upload this slot's mapped instance upload buffer.
     */
    void copyPendingToUploadBuffer(std::vector<uint32_t>& indicesPendingUpload,
                                   std::span<const GPUMeshInstance> instances,
                                   const MappedSpan<GPUMeshInstance>& upload);

    std::array<detail::MeshDrawResourceSlot, FRAMES_IN_FLIGHT> slots_;
    BuildDrawCommands buildDrawCommands_;

    std::vector<vk::BufferCopy> copyRegions_;
    std::vector<vk::BufferMemoryBarrier2> barriers_;
};
