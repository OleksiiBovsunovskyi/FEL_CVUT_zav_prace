module;

#include <cstdint>
#include <filesystem>

export module BuildDrawCommands;

import vulkan;
import GPUTypes;
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

    [[nodiscard]] bool init(vk::Device device, ShaderLoader& shaderLoader,
                            const std::filesystem::path& shaderPath);

    /**
     * Records one compute invocation per object, in groups of 64 
     *
     * @param push buffer addresses plus the object count, which the tail
     *        invocations of the last group exit on.
     */
    void record(vk::CommandBuffer commandBuffer,
                const BuildDrawCommandsPush& push) const;

    void destroy();

private:
    static constexpr uint32_t WORKGROUP_SIZE = 64;

    vk::Device         device_         = nullptr;
    vk::PipelineLayout pipelineLayout_ = nullptr;
    vk::Pipeline       pipeline_       = nullptr;
};
