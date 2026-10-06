module;

#include <cstdint>
#include <filesystem>
#include <vector>

export module DepthPyramid;

import vulkan;
import ComputePass;
import Frame;
import GPUTypes;
import GpuPassTimings;
import ShadersLoader;
import VK_Buffers;
import VulkanContext;

/**
 * Where the shaders find a built depth pyramid.
 */
export struct DepthPyramidView {
    /// Level 0 first, every level packed after the one above it.
    GpuPtr<GPUHiZTexel> data{};
    /// Level 0 size in texels.
    vk::Extent2D        extent{};
    uint32_t            levels = 0;

    [[nodiscard]] explicit operator bool() const {
        return data.address != 0 && levels != 0;
    }
};

/**
 * Depth and its downsampled variants
 * Next level : min of 4 neighbors
 */
export class DepthPyramid {
public:
    DepthPyramid() = default;
    ~DepthPyramid() { destroy(); }

    DepthPyramid(const DepthPyramid&)            = delete;
    DepthPyramid& operator=(const DepthPyramid&) = delete;

    /**
     * @param ctx supplies the device and the VMA allocator.
     * @param reduceShaderPath Shader that does depth texture reduction.
     * @param extent size of the depth images it will be built from.
     * @return true on success.
     */
    [[nodiscard]] bool init(VulkanContext& ctx, ShaderLoader& shaderLoader,
                            const std::filesystem::path& reduceShaderPath,
                            vk::Extent2D extent);

    void destroy();

    /**
     * Reallocates the buffer for a new depth size.
     * @param extent size of the depth images it will be built from.
     * @return true on success, false otherwise.
     * @note The GPU must be done with the buffer before this is called.
     */
    [[nodiscard]] bool resize(vk::Extent2D extent);

    /**
     * Copies the depth image into level 0 and reduces the remaining levels.
     * @param recording active frame recording.
     * @param depth depth image in eDepthAttachmentOptimal, its depth writes finished by the caller's rendering.
     * @param depthExtent size of `depth` in texels.
     * @param timings marked after the last level.
     * @return the built pyramid, empty when `depthExtent` differs from the size this was allocated for.
     * @note Leaves `depth` in eDepthAttachmentOptimal.
     */
    [[nodiscard]] DepthPyramidView build(Frame::Recording& recording, vk::Image depth,
                                         vk::Extent2D depthExtent,
                                         GpuPassTimings& timings);

private:
    AllocatedBuffer<DeviceOnlyBuffer>  texels_;
    ComputePass<DepthPyramidReducePush> reduce_;
    VulkanContext*                     ctx_ = nullptr;
    vk::Extent2D                       extent_{};
    uint32_t                           levels_ = 0;

    std::vector<vk::BufferMemoryBarrier2> barriers_;
};
