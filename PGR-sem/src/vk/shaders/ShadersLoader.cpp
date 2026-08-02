module;
#include <vulkan/vulkan.h>

#include <shaderc/shaderc.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module ShadersLoader;

import Logger;

namespace fs = std::filesystem;

namespace {

constexpr size_t MAX_INCLUDE_DEPTH = 32;

bool readTextFile(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    f.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    out.resize(size);
    f.read(out.data(), static_cast<std::streamsize>(size));
    return f.good() || f.eof();
}

shaderc_shader_kind toShadercKind(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:      return shaderc_vertex_shader;
        case ShaderStage::Fragment:    return shaderc_fragment_shader;
        case ShaderStage::Compute:     return shaderc_compute_shader;
        case ShaderStage::Geometry:    return shaderc_geometry_shader;
        case ShaderStage::TessControl: return shaderc_tess_control_shader;
        case ShaderStage::TessEval:    return shaderc_tess_evaluation_shader;
        case ShaderStage::Task:        return shaderc_task_shader;
        case ShaderStage::Mesh:        return shaderc_mesh_shader;
    }
    return shaderc_vertex_shader;
}

/**
 * Resolves #include for shaderc.
 */
class FileIncluder final : public shaderc::CompileOptions::IncluderInterface {
public:
    FileIncluder(const std::vector<fs::path>& searchDirs, std::vector<fs::path>* deps)
        : searchDirs_(searchDirs), deps_(deps) {}

    shaderc_include_result* GetInclude(const char* requestedSource,
                                       shaderc_include_type type,
                                       const char* requestingSource,
                                       size_t includeDepth) override {
        auto payload = std::make_unique<Payload>();

        if (includeDepth > MAX_INCLUDE_DEPTH) {
            payload->content = std::string("include depth limit reached including '") +
                               requestedSource + "' (cyclic include?)";
            return makeResult(std::move(payload));
        }

        fs::path resolved;
        if (!resolve(requestedSource, type, requestingSource, resolved)) {
            payload->content = std::string("cannot open include '") + requestedSource +
                               "' requested by '" + requestingSource + "'";
            return makeResult(std::move(payload));
        }

        if (!readTextFile(resolved, payload->content)) {
            payload->content = "cannot read " + resolved.string();
            return makeResult(std::move(payload));
        }

        payload->name = resolved.string();
        if (deps_) deps_->push_back(resolved);
        return makeResult(std::move(payload));
    }

    void ReleaseInclude(shaderc_include_result* data) override {
        if (!data) return;
        delete static_cast<Payload*>(data->user_data);
        delete data;
    }

private:
    struct Payload {
        std::string name;
        std::string content;
    };

    bool resolve(const char* requested, shaderc_include_type type,
                 const char* requestingSource, fs::path& out) const {
        const fs::path requestedPath(requested);

        if (type == shaderc_include_type_relative && requestingSource) {
            const fs::path parent = fs::path(requestingSource).parent_path();
            if (!parent.empty()) {
                const fs::path candidate = parent / requestedPath;
                if (fs::exists(candidate)) {
                    out = fs::weakly_canonical(candidate);
                    return true;
                }
            }
        }

        for (const fs::path& dir : searchDirs_) {
            const fs::path candidate = dir / requestedPath;
            if (fs::exists(candidate)) {
                out = fs::weakly_canonical(candidate);
                return true;
            }
        }
        return false;
    }

    /// Both allocations are handed to shaderc and freed in ReleaseInclude.
    static shaderc_include_result* makeResult(std::unique_ptr<Payload> payload) {
        Payload* p = payload.release();

        auto* result = new shaderc_include_result{};
        result->source_name        = p->name.c_str();
        result->source_name_length = p->name.size();
        result->content            = p->content.c_str();
        result->content_length     = p->content.size();
        result->user_data          = p;
        return result;
    }

    const std::vector<fs::path>& searchDirs_;
    std::vector<fs::path>*       deps_;
};

} // namespace


void ShaderLoader::addIncludeDir(fs::path dir) {
    includeDirs_.push_back(std::move(dir));
}

void ShaderLoader::define(std::string name, std::string value) {
    defines_.emplace_back(std::move(name), std::move(value));
}

bool ShaderLoader::stageFromExtension(const fs::path& path, ShaderStage& outStage) {
    const std::string ext = path.extension().string();

    if (ext == ".vert") { outStage = ShaderStage::Vertex;      return true; }
    if (ext == ".frag") { outStage = ShaderStage::Fragment;    return true; }
    if (ext == ".comp") { outStage = ShaderStage::Compute;     return true; }
    if (ext == ".geom") { outStage = ShaderStage::Geometry;    return true; }
    if (ext == ".tesc") { outStage = ShaderStage::TessControl; return true; }
    if (ext == ".tese") { outStage = ShaderStage::TessEval;    return true; }
    if (ext == ".task") { outStage = ShaderStage::Task;        return true; }
    if (ext == ".mesh") { outStage = ShaderStage::Mesh;        return true; }
    return false;
}

bool ShaderLoader::compile(const fs::path& path, ShaderStage stage,
                           std::vector<uint32_t>& outSpirv,
                           std::vector<fs::path>* outDeps) const {
    std::string source;
    if (!readTextFile(path, source)) {
        logError("ShaderLoader: cannot open " + path.string());
        return false;
    }

    shaderc::CompileOptions options;
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);   // what Vulkan 1.3 consumes

    for (const auto& [name, value] : defines_)
        options.AddMacroDefinition(name, value);

    if (optimize_) {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
    } else {
        options.SetGenerateDebugInfo();
        options.SetOptimizationLevel(shaderc_optimization_level_zero);
    }

    options.SetIncluder(std::make_unique<FileIncluder>(includeDirs_, outDeps));

    const std::string name = path.string();

    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(source, toShadercKind(stage),
                                                  name.c_str(), "main", options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {

        logError("ShaderLoader: " + result.GetErrorMessage());
        return false;
    }
    if (const size_t warnings = result.GetNumWarnings(); warnings > 0)
        logWarning("ShaderLoader: " + name + ": " + result.GetErrorMessage());

    outSpirv.assign(result.cbegin(), result.cend());
    if (outDeps) outDeps->push_back(path);
    return true;
}

VkShaderModule ShaderLoader::createModule(const std::vector<uint32_t>& spirv,
                                          const std::string& debugName) const {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spirv.size() * sizeof(uint32_t);   // in bytes, not words
    info.pCode    = spirv.data();

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult r = vkCreateShaderModule(device_, &info, nullptr, &module);
    if (r != VK_SUCCESS) {
        logError("ShaderLoader: vkCreateShaderModule failed for " + debugName +
                 ": VkResult " + std::to_string(r));
        return VK_NULL_HANDLE;
    }
    return module;
}

VkShaderModule ShaderLoader::load(const fs::path& path, std::vector<fs::path>* outDeps) {
    if (path.extension() == ".spv")
        return loadSpv(path);

    ShaderStage stage{};
    if (!stageFromExtension(path, stage)) {
        logError("ShaderLoader: cannot infer a stage from '" + path.string() +
                 "'; call the overload that takes a ShaderStage");
        return VK_NULL_HANDLE;
    }
    return load(path, stage, outDeps);
}

VkShaderModule ShaderLoader::load(const fs::path& path, ShaderStage stage,
                                  std::vector<fs::path>* outDeps) {
    if (path.extension() == ".spv")
        return loadSpv(path);

    std::vector<uint32_t> spirv;
    if (!compile(path, stage, spirv, outDeps))
        return VK_NULL_HANDLE;

    return createModule(spirv, path.string());
}

VkShaderModule ShaderLoader::loadSpv(const fs::path& path) const {
    std::string bytes;
    if (!readTextFile(path, bytes)) {
        logError("ShaderLoader: cannot open " + path.string());
        return VK_NULL_HANDLE;
    }
    if (bytes.empty() || bytes.size() % sizeof(uint32_t) != 0) {
        logError("ShaderLoader: " + path.string() + " is not a whole number of SPIR-V words");
        return VK_NULL_HANDLE;
    }

    std::vector<uint32_t> spirv(bytes.size() / sizeof(uint32_t));
    std::memcpy(spirv.data(), bytes.data(), bytes.size());
    return createModule(spirv, path.string());
}
