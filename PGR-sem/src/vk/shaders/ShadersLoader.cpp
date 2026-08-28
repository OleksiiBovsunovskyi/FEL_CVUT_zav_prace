module;
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

module ShadersLoader;

import Logger;

namespace fs = std::filesystem;

namespace {

bool readBinaryFile(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    f.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    out.resize(size);
    f.read(out.data(), static_cast<std::streamsize>(size));
    return f.good() || f.eof();
}

} // namespace

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

VkShaderModule ShaderLoader::load(const fs::path& path) const {
    std::string bytes;
    if (!readBinaryFile(path, bytes)) {
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
