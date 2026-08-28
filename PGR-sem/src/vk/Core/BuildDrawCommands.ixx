module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

export module BuildDrawCommands;

import ShadersLoader;

/**
 * Owns and records the compute pass implemented by build_draw_commands.comp.
 */
export class BuildDrawCommands {
public:
    BuildDrawCommands() = default;
    ~BuildDrawCommands();

    BuildDrawCommands(const BuildDrawCommands&) = delete;
    BuildDrawCommands& operator=(const BuildDrawCommands&) = delete;

    [[nodiscard]] bool init(VkDevice device, ShaderLoader& shaders,
                            const std::filesystem::path& shaderPath);

    /// Records one compute invocation per object, in groups of 64 ///!TODO: Ask driver about line count.
    void record(VkCommandBuffer commandBuffer, uint32_t objectCount) const;

    void destroy();

private:
    static constexpr uint32_t WORKGROUP_SIZE = 64;

    VkDevice         device_         = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;
};
