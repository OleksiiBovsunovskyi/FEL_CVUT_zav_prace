module;

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

module ShadersLoader;

import vulkan;
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

vk::ShaderModule ShaderLoader::createModule(const std::vector<uint32_t>& spirv,
                                            const std::string& debugName) const {
    vk::ShaderModuleCreateInfo info{};
    info.codeSize = spirv.size() * sizeof(uint32_t);   // in bytes, not words
    info.pCode    = spirv.data();

    vk::ShaderModule module = nullptr;
    const vk::Result r = device_.createShaderModule(&info, nullptr, &module);
    if (r != vk::Result::eSuccess) {
        logError("ShaderLoader: vkCreateShaderModule failed for " + debugName +
                 ": VkResult " + vk::to_string(r));
        return nullptr;
    }
    return module;
}

vk::ShaderModule ShaderLoader::load(const fs::path& path) const {
    std::string bytes;
    if (!readBinaryFile(path, bytes)) {
        logError("ShaderLoader: cannot open " + path.string());
        return nullptr;
    }
    if (bytes.empty() || bytes.size() % sizeof(uint32_t) != 0) {
        logError("ShaderLoader: " + path.string() + " is not a whole number of SPIR-V words");
        return nullptr;
    }

    std::vector<uint32_t> spirv(bytes.size() / sizeof(uint32_t));
    std::memcpy(spirv.data(), bytes.data(), bytes.size());
    return createModule(spirv, path.string());
}
