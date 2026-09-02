module;
#include <vulkan/vulkan.h>

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
    [[nodiscard]] bool init(VkDevice device, ShaderLoader& shaders,
                            const std::filesystem::path& meshShaderPath,
                            const std::filesystem::path& fragmentShaderPath,
                            VkFormat colorFormat, VkFormat depthFormat,
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
    void record(VkCommandBuffer commandBuffer, VkExtent2D extent,
                const MeshDrawPush& push,
                const BufferRegion& commands, const BufferRegion& count,
                uint32_t maxDrawCount) const;

    void destroy();

private:
    VkDevice         device_         = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;

    PFN_vkCmdDrawMeshTasksIndirectCountEXT drawIndirectCount_ = nullptr;
};
