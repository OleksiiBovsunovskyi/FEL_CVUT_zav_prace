module;

#include <filesystem>
#include <memory>

export module ShadersLoader;

import vulkan;

/**
 * Compiles .slang sources to SPIR-V at runtime and wraps the result in a vk::ShaderModule.
 * Each source declares exactly one entry point, for example [shader("compute")] void main(...).
 * The SPIR-V entry point is named "main".
 */
export class ShaderLoader {
public:
    ShaderLoader();
    ~ShaderLoader();

    ShaderLoader(const ShaderLoader&)            = delete;
    ShaderLoader& operator=(const ShaderLoader&) = delete;

    /**
     * Creates the Slang session.
     * @param device device the shader modules are created on
     * @param shaderRoot directory holding the .slang sources; it and its subdirectories are import search paths
     * @return false when the Slang session cannot be created
     */
    [[nodiscard]] bool init(vk::Device device, const std::filesystem::path& shaderRoot);

    /**
     * Compiles one .slang source and creates its shader module.
     * @param path source path relative to the shader root
     * @return nullptr when the source does not compile or does not declare exactly one entry point
     */
    [[nodiscard]] vk::ShaderModule load(const std::filesystem::path& path) const;

    void destroy(vk::ShaderModule module) const {
        if (module) device_.destroyShaderModule(module);
    }

private:
    struct Compiler;

    vk::Device                device_ = nullptr;
    std::filesystem::path     root_;
    std::unique_ptr<Compiler> compiler_;
};
