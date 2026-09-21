module;
#include "pgr.h"
#include <string>
#include <vector>
#include <iostream>
export module postprocessshader;

import Logger.gl;
import ShaderLoader;

/**
 * Shader used by post process effects
 */
export class PostProcessShader {
public:
    PostProcessShader(
        const std::string &vertexShaderPath,
        const std::string &fragmentShaderPath,
        const GLint minFilter = GL_LINEAR,
        const GLint magFilter = GL_LINEAR,
        const GLint wrapS = GL_CLAMP_TO_EDGE,
        const GLint wrapT = GL_CLAMP_TO_EDGE) : minFilter(minFilter),
                                                magFilter(magFilter),
                                                wrapS(wrapS),
                                                wrapT(wrapT) {
        shaders = {
            ShaderLoader::createShader(GL_VERTEX_SHADER, vertexShaderPath),
            ShaderLoader::createShader(GL_FRAGMENT_SHADER, fragmentShaderPath)
        };
        if (!shaders[0] || !shaders[1]) {
            logError("Failed to compile shaders for PostProcessShader");
            isError = true;
            return;
        }
        shaderProgram = pgr::createProgram(shaders);
        if (!shaderProgram) {
            logError("Failed to create shader program for PostProcessShader");
            isError = true;
            return;
        }
    }

    ~PostProcessShader() {
        if (shaderProgram) {
            pgr::deleteProgramAndShaders(shaderProgram);
            shaderProgram = 0;
        }
    }

    bool Init(int w, int h) {
        if (isError) {
            logError("PostProcessShader: initialization failed due to previous errors");
            return false;
        }
        width = w;
        height = h;
        return true;
    }

    void Enable() { isEnabled = true; }
    void Disable() { isEnabled = false; }
    void Toggle() { isEnabled = !isEnabled; }

    [[nodiscard]] bool isEffectEnabled() const { return isEnabled; }
    
    
    [[nodiscard]] bool Render(GLuint quadVAO) const {
        if (!isEnabled || !shaderProgram) return false;
        if (isError) {
            logError("PostProcessShader: render failed due to previous errors");
            return false;
        }

        glClear(GL_COLOR_BUFFER_BIT);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        return true;
    }
    
    void Use() const { glUseProgram(shaderProgram); }

    [[nodiscard]] bool SetUniform(const std::string &name, int value) const {
        GLint loc = glGetUniformLocation(shaderProgram, name.c_str());
        if (loc == -1) {
            logWarning("PostProcessShader: uniform '" + name + "' not found in shader");
            return false;
        }
        glUniform1i(loc, value);
        return true;
    }

    [[nodiscard]] bool SetUniform(const std::string &name, bool value) const {
        return SetUniform(name, (int) value);
    }

    [[nodiscard]] bool SetUniform(const std::string &name, float value) const {
        GLint loc = glGetUniformLocation(shaderProgram, name.c_str());
        if (loc == -1) {
            logWarning("PostProcessShader: uniform '" + name + "' not found in shader");
            return false;
        }
        glUniform1f(loc, value);
        return true;
    }

    [[nodiscard]] bool SetUniform(const std::string &name, const glm::vec2 &value) const {
        GLint loc = glGetUniformLocation(shaderProgram, name.c_str());
        if (loc == -1) {
            logWarning("PostProcessShader: uniform '" + name + "' not found in shader");
            return false;
        }
        glUniform2f(loc, value.x, value.y);
        return true;
    }

    [[nodiscard]] bool SetUniform(const std::string &name, const glm::vec3 &value) const {
        GLint loc = glGetUniformLocation(shaderProgram, name.c_str());
        if (loc == -1) {
            logWarning("PostProcessShader: uniform '" + name + "' not found in shader");
            return false;
        }  
        glUniform3f(loc, value.x, value.y, value.z);
        return true;
    }

    [[nodiscard]] bool SetUniform(const std::string &name, const glm::vec4 &value) const {
        GLint loc = glGetUniformLocation(shaderProgram, name.c_str());
        if (loc == -1) {
            logWarning("PostProcessShader: uniform '" + name + "' not found in shader");
            return false;
        }
        glUniform4f(loc, value.x, value.y, value.z, value.w);
        return true;
    }

    [[nodiscard]] bool SetUniform(const std::string &name, glm::mat4 value) const {
        GLint loc = glGetUniformLocation(shaderProgram, name.c_str());
        if (loc == -1) {
            logWarning("PostProcessShader: uniform '" + name + "' not found in shader");
            return false;
        }
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
        return true;
    }

private:
    bool isEnabled = true;
    bool isError = false;
    std::vector<GLuint> shaders;
    GLuint shaderProgram = 0;
    int width = 0;
    int height = 0;
    GLint minFilter;
    GLint magFilter;
    GLint wrapS;
    GLint wrapT;
};
