module;
#include "pgr.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

export module fog.postprocesseffect;

import PostProcessEffect;
import postprocessshader;
import RenderBufferManager;   
import Logger.gl; 

export class FogPostProcessEffect : public IPostProcessEffect {
public:
    FogPostProcessEffect(const RenderBufferManager& buffers)
        : shader(
            "Shaders/PostProcess/Fog/fogVrtx.glsl",
            "Shaders/PostProcess/Fog/fogFrag.glsl"),
          bufferManager(buffers) {
    }

    ~FogPostProcessEffect() = default;

    [[nodiscard]] bool Init(const int width, const int height) override {
        if (!shader.Init(width, height)) return false;
        shader.Use();
        if (!shader.SetUniform("uScene", getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logWarning("FogPostProcessEffect: failed to set uniform 'uScene'");
        if (!shader.SetUniform("uDepth", getNamedBufferTextureUnit(ENamedBuffer::Depth)))
            logWarning("FogPostProcessEffect: failed to set uniform 'uDepth'");
        return true;
    }

    void operator()(GLuint quadVAO, int width, int height) override {
        shader.Use();
        if (!bufferManager.useBuffer(ENamedBuffer::SceneColor, getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logError("FogPostProcessEffect: failed to bind SceneColor buffer");
        if (!bufferManager.useBuffer(ENamedBuffer::Depth, getNamedBufferTextureUnit(ENamedBuffer::Depth)))
            logError("FogPostProcessEffect: failed to bind Depth buffer");
        if (!shader.SetUniform("uInvViewProj",      invViewProj))
            logError("FogPostProcessEffect: failed to set uniform 'uInvViewProj'");
        if (!shader.SetUniform("uCameraPos",         cameraPos))
            logError("FogPostProcessEffect: failed to set uniform 'uCameraPos'");
        if (!shader.SetUniform("uFogDepthDensity",   fogDepthDensity))
            logError("FogPostProcessEffect: failed to set uniform 'uFogDepthDensity'");
        if (!shader.SetUniform("uFogHeightDensity",  fogHeightDensity))
            logError("FogPostProcessEffect: failed to set uniform 'uFogHeightDensity'");
        if (!shader.SetUniform("uFogStartDepth",     fogStartDepth))
            logError("FogPostProcessEffect: failed to set uniform 'uFogStartDepth'");
        if (!shader.SetUniform("uFogEndDepth",       fogEndDepth))
            logError("FogPostProcessEffect: failed to set uniform 'uFogEndDepth'");
        if (!shader.SetUniform("uFogStartHeight",    fogStartHeight))
            logError("FogPostProcessEffect: failed to set uniform 'uFogStartHeight'");
        if (!shader.SetUniform("uFogEndHeight",      fogEndHeight))
            logError("FogPostProcessEffect: failed to set uniform 'uFogEndHeight'");
        if (!shader.SetUniform("uFogColor",          fogColor))
            logError("FogPostProcessEffect: failed to set uniform 'uFogColor'");
        if (!shader.Render(quadVAO))
            logError("FogPostProcessEffect: render failed");
    }

    void SetMatrices(const glm::mat4& view, const glm::mat4& projection) {
        invViewProj = glm::inverse(projection * view);
    }

    void SetCameraPos(const glm::vec3& pos) {
        cameraPos = pos;
    }

    float     nearPlane        = 0.1f;
    float     farPlane         = 200.0f;
    float     fogDepthDensity  = 1; 
    float     fogHeightDensity = 2;
    float     fogStartDepth    = 0.0f;
    float     fogEndDepth      = 60.0f;
    float     fogStartHeight   = -25.0f;
    float     fogEndHeight     =  60.0f;
    glm::vec3 fogColor         = glm::vec3(0.7f, 0.75f, 0.8f);

private:
    PostProcessShader          shader;
    const RenderBufferManager& bufferManager;
    glm::mat4                  invViewProj = glm::mat4(1.0f);
    glm::vec3                  cameraPos   = glm::vec3(0.0f);
};
