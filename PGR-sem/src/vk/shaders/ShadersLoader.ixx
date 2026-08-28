module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

export module ShadersLoader;

/**
 * SPIR-V -> VkShaderModule. The .spv files are produced from the .slang
 * sources by slangc at build time; nothing is compiled at runtime.
 */
export class ShaderLoader {
public:
    ShaderLoader() = default;
    ~ShaderLoader() = default;

    ShaderLoader(const ShaderLoader&)            = delete;
    ShaderLoader& operator=(const ShaderLoader&) = delete;

    void init(VkDevice device) { device_ = device; }

    /// @return VK_NULL_HANDLE when the file is missing or not whole SPIR-V words.
    [[nodiscard]] VkShaderModule load(const std::filesystem::path& path) const;

    [[nodiscard]] VkShaderModule createModule(const std::vector<uint32_t>& spirv,
                                              const std::string& debugName) const;

    void destroy(VkShaderModule module) const {
        if (module) vkDestroyShaderModule(device_, module, nullptr);
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
};
