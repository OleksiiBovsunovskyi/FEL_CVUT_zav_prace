module;
#include "pgr.h"

export module BlitShader;

import Logger.gl;
import ShaderLoader;

export class BlitShader {
public:
    BlitShader() = default;

    ~BlitShader() {
        if (program) { pgr::deleteProgramAndShaders(program); program = 0; }
    }

    [[nodiscard]] bool init() {
        const GLuint shaders[] = {
            ShaderLoader::createShader(GL_VERTEX_SHADER,   "Shaders/Default/Blit/blitVrtx.glsl"),
            ShaderLoader::createShader(GL_FRAGMENT_SHADER, "Shaders/Default/Blit/blitFrag.glsl"),
            0
        };
        if (!shaders[0] || !shaders[1]) {
            logError("BlitShader: failed to compile shaders");
            return false;
        }
        program = pgr::createProgram(shaders);
        if (!program) { 
            logError("BlitShader: failed to link program");
            return false;
        }
        sourceLoc = glGetUniformLocation(program, "sourceTexture");
        return true;
    }

    // Blits srcTex into the currently bound FBO
    void blit(GLuint srcTex, GLuint quadVAO) const {
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glUniform1i(sourceLoc, 0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glUseProgram(0);
    }

private:
    GLuint program  = 0;
    GLint sourceLoc = -1;
};
