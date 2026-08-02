module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

export module ShadersLoader;

/**
 * GLSL -> SPIR-V -> VkShaderModule, compiled at runtime by shaderc
 * Shaders using #include must declare it:
 *
 *     #extension GL_GOOGLE_include_directive : require
 *     #include "common.glsl"
 */
export enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEval,
    Task,
    Mesh,
};

export class ShaderLoader {
public:
    ShaderLoader() = default;
    ~ShaderLoader() = default;

    ShaderLoader(const ShaderLoader&)            = delete;
    ShaderLoader& operator=(const ShaderLoader&) = delete;

    void init(VkDevice device) { device_ = device; }

    /**
     * Searched in order for `#include <...>`, and after the includer's own
     * directory for `#include "..."`.
     */
    void addIncludeDir(std::filesystem::path dir);

    /// Prepended to every compile, as if written at the top of the source.
    void define(std::string name, std::string value = {});

    /**
     * Off (the default) keeps SPIR-V debug info: readable names and source
     * lines in RenderDoc and in validation-layer messages. Turn on for release.
     */
    void setOptimize(bool enable) { optimize_ = enable; }

    /**
     * Stage is inferred from the extension: .vert .frag .comp .geom .tesc
     * .tese .task .mesh - or the file is loaded as-is if it ends in .spv.
     */
    [[nodiscard]] VkShaderModule load(const std::filesystem::path& path,
                                      std::vector<std::filesystem::path>* outDeps = nullptr);

    /// Explicit stage, for sources whose extension does not say.
    [[nodiscard]] VkShaderModule load(const std::filesystem::path& path, ShaderStage stage,
                                      std::vector<std::filesystem::path>* outDeps = nullptr);

    /// Compile without creating a module
    [[nodiscard]] bool compile(const std::filesystem::path& path, ShaderStage stage,
                               std::vector<uint32_t>& outSpirv,
                               std::vector<std::filesystem::path>* outDeps = nullptr) const;

    [[nodiscard]] VkShaderModule createModule(const std::vector<uint32_t>& spirv,
                                              const std::string& debugName) const;

    void destroy(VkShaderModule module) const {
        if (module) vkDestroyShaderModule(device_, module, nullptr);
    }

    /// False if the extension does not name a known stage.
    [[nodiscard]] static bool stageFromExtension(const std::filesystem::path& path,
                                                 ShaderStage& outStage);

private:
    VkDevice device_ = VK_NULL_HANDLE;

    std::vector<std::filesystem::path> includeDirs_;
    std::vector<std::pair<std::string, std::string>> defines_;
    bool optimize_ = false;

    [[nodiscard]] VkShaderModule loadSpv(const std::filesystem::path& path) const;
};
