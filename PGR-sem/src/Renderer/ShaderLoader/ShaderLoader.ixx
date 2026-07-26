module;
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "pgr.h"
export module ShaderLoader;

/**
 * Resolves #include for shaders loaded with this helper
 */
namespace ShaderLoader {

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ShaderLoader] Cannot open: " << path << "\n";
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string resolve(const std::string& source) {
    std::string result;
    result.reserve(source.size());
    std::istringstream stream(source);
    std::string line;

    while (std::getline(stream, line)) {
        auto p = line.find("#include");
        if (p != std::string::npos) {
            auto q1 = line.find('"', p + 8);
            auto q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string includePath = line.substr(q1 + 1, q2 - q1 - 1);
                std::string included = readFile(includePath);
                if (!included.empty())
                    result += resolve(included);
                result += '\n'; 
                continue;
            }
        }
        result += line;
        result += '\n';
    }
    return result;
}

export GLuint createShader(GLenum type, const std::string& path) {
    std::string source = readFile(path);
    if (source.empty()) return 0;
    return pgr::createShaderFromSource(type, resolve(source));
}

}
