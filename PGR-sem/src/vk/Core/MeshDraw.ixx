module;
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <filesystem>

export module MeshDraw;

import GPUTypes;
import ShadersLoader;
import VK_Buffers;

/**
 * Owns the mesh-shader graphics pass and issues the indirect draw whose
 * commands build_draw_commands.comp produced.
 */
export class MeshDraw {
public:
    MeshDraw() = default;
    ~MeshDraw();

    MeshDraw(const MeshDraw&) = delete;
    MeshDraw& operator=(const MeshDraw&) = delete;

    /**
     * @param drawIndirectCount vkCmdDrawMeshTasksIndirectCountEXT, from
     *        VulkanContext.
     * @param colorFormat swapchain format the pass renders into.
     */
    [[nodiscard]] bool init(vk::Device device, ShaderLoader& shaderLoader,
                            const std::filesystem::path& meshShaderPath,
                            const std::filesystem::path& fragmentShaderPath,
                            vk::Format colorFormat, vk::Format depthFormat,
                            PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount);

    /**
     * Records one indirect draw. Must be inside a render pass whose attachment
     * formats match those given to init().
     *
     * @param commands the GPUMeshTaskCommand array built by the compute pass.
     * @param count the draw count it wrote; both offsets must be 4-byte aligned.
     * @param maxDrawCount upper bound the count is clamped against - the object
     *        count the command buffer was sized for.
     */
    void record(vk::CommandBuffer commandBuffer, vk::Extent2D extent,
                const GPUMeshDrawPush& push,
                const BufferRegion& commands, const BufferRegion& count,
                uint32_t maxDrawCount) const;

    void destroy();

private:
    vk::Device         device_         = nullptr;
    vk::PipelineLayout pipelineLayout_ = nullptr;
    vk::Pipeline       pipeline_       = nullptr;

    PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount_ = nullptr;
};
