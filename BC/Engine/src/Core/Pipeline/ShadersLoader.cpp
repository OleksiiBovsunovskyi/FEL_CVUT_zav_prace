module;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

module ShadersLoader;

import vulkan;
import Logger;

namespace fs = std::filesystem;

/// Slang global session and the one compile session every load() goes through.
struct ShaderLoader::Compiler {
    Slang::ComPtr<slang::IGlobalSession> global;
    Slang::ComPtr<slang::ISession>       session;
};

namespace {

/// @return the diagnostics text, or an empty string when there is none.
std::string diagnosticText(slang::IBlob* diagnostics) {
    if (!diagnostics) return {};
    return {static_cast<const char*>(diagnostics->getBufferPointer()),
            diagnostics->getBufferSize()};
}

/**
 * Logs Slang diagnostics.
 * @param failed true when the step that produced them failed
 */
void logDiagnostics(const fs::path& path, slang::IBlob* diagnostics, bool failed) {
    const std::string text = diagnosticText(diagnostics);
    if (text.empty()) return;
    if (failed) logError("ShaderLoader: " + path.string() + ":\n" + text);
    else        logWarning("ShaderLoader: " + path.string() + ":\n" + text);
}

} // namespace

ShaderLoader::ShaderLoader() = default;
ShaderLoader::~ShaderLoader() = default;

bool ShaderLoader::init(vk::Device device, const fs::path& shaderRoot) {
    device_ = device;
    root_   = shaderRoot;

    auto compiler = std::make_unique<Compiler>();
    if (SLANG_FAILED(slang::createGlobalSession(compiler->global.writeRef()))) {
        logError("ShaderLoader: cannot create the Slang global session");
        return false;
    }

    std::vector<std::string> searchPaths{shaderRoot.string()};
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(shaderRoot, ec))
        if (entry.is_directory()) searchPaths.push_back(entry.path().string());
    if (ec) {
        logError("ShaderLoader: cannot read shader directory " + shaderRoot.string());
        return false;
    }
    std::vector<const char*> searchPathPtrs;
    for (const std::string& p : searchPaths) searchPathPtrs.push_back(p.c_str());

    slang::CompilerOptionEntry emitSpirvDirectly{};
    emitSpirvDirectly.name         = slang::CompilerOptionName::EmitSpirvDirectly;
    emitSpirvDirectly.value.kind   = slang::CompilerOptionValueKind::Int;
    emitSpirvDirectly.value.intValue0 = 1;

    slang::TargetDesc target{};
    target.format                   = SLANG_SPIRV;
    target.profile                  = compiler->global->findProfile("spirv_1_6");
    target.compilerOptionEntries    = &emitSpirvDirectly;
    target.compilerOptionEntryCount = 1;

    slang::SessionDesc sessionDesc{};
    sessionDesc.targets         = &target;
    sessionDesc.targetCount     = 1;
    sessionDesc.searchPaths     = searchPathPtrs.data();
    sessionDesc.searchPathCount = static_cast<SlangInt>(searchPathPtrs.size());
    // Same matrix layout as slangc, whose SPIR-V gpu_layout_check.cpp is generated from.
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    if (SLANG_FAILED(compiler->global->createSession(sessionDesc, compiler->session.writeRef()))) {
        logError("ShaderLoader: cannot create the Slang session");
        return false;
    }
    compiler_ = std::move(compiler);
    return true;
}

vk::ShaderModule ShaderLoader::load(const fs::path& path) const {
    const fs::path fullPath = root_ / path;
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        logError("ShaderLoader: cannot open " + fullPath.string());
        return nullptr;
    }
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = compiler_->session->loadModuleFromSourceString(
        path.stem().string().c_str(), fullPath.string().c_str(), source.c_str(),
        diagnostics.writeRef());
    logDiagnostics(fullPath, diagnostics, !module);
    if (!module) return nullptr;

    if (module->getDefinedEntryPointCount() != 1) {
        logError("ShaderLoader: " + fullPath.string() + " declares " +
                 std::to_string(module->getDefinedEntryPointCount()) +
                 " entry points; exactly one is required");
        return nullptr;
    }
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(module->getDefinedEntryPoint(0, entryPoint.writeRef()))) {
        logError("ShaderLoader: cannot read the entry point of " + fullPath.string());
        return nullptr;
    }

    slang::IComponentType* parts[] = {module, entryPoint};
    Slang::ComPtr<slang::IComponentType> program;
    diagnostics = nullptr;
    const bool composed = SLANG_SUCCEEDED(compiler_->session->createCompositeComponentType(
        parts, 2, program.writeRef(), diagnostics.writeRef()));
    logDiagnostics(fullPath, diagnostics, !composed);
    if (!composed) return nullptr;

    Slang::ComPtr<slang::IComponentType> linked;
    diagnostics = nullptr;
    const bool wasLinked = SLANG_SUCCEEDED(program->link(linked.writeRef(), diagnostics.writeRef()));
    logDiagnostics(fullPath, diagnostics, !wasLinked);
    if (!wasLinked) return nullptr;

    Slang::ComPtr<slang::IBlob> spirv;
    diagnostics = nullptr;
    const bool compiled = SLANG_SUCCEEDED(
        linked->getEntryPointCode(0, 0, spirv.writeRef(), diagnostics.writeRef()));
    logDiagnostics(fullPath, diagnostics, !compiled);
    if (!compiled) return nullptr;

    vk::ShaderModuleCreateInfo info{};
    info.codeSize = spirv->getBufferSize();
    info.pCode    = static_cast<const uint32_t*>(spirv->getBufferPointer());

    vk::ShaderModule shaderModule = nullptr;
    const vk::Result r = device_.createShaderModule(&info, nullptr, &shaderModule);
    if (r != vk::Result::eSuccess) {
        logError("ShaderLoader: vkCreateShaderModule failed for " + fullPath.string() +
                 ": VkResult " + vk::to_string(r));
        return nullptr;
    }
    return shaderModule;
}
