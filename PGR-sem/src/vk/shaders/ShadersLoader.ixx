module;
#include <vulkan/vulkan.hpp>

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

    void init(vk::Device device) { device_ = device; }

    /// @return nullptr when the file is missing or not whole SPIR-V words.
    [[nodiscard]] vk::ShaderModule load(const std::filesystem::path& path) const;

    [[nodiscard]] vk::ShaderModule createModule(const std::vector<uint32_t>& spirv,
                                                const std::string& debugName) const;

    void destroy(vk::ShaderModule module) const {
        if (module) device_.destroyShaderModule(module);
    }

private:
    vk::Device device_ = nullptr;
};
