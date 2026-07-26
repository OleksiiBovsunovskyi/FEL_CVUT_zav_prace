module;
#include "pgr.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
export module ssr.postprocesseffect;


import PostProcessEffect;
import postprocessshader;
import RenderBufferManager;
import Logger;

export class SSRPostProcessEffect : public IPostProcessEffect {
public:
    SSRPostProcessEffect(const RenderBufferManager& buffers)
        : shader(
            "Shaders/PostProcess/SSR/SSRVrtx.glsl",
            "Shaders/PostProcess/SSR/SSRFrag.glsl"),
          bufferManager(buffers) {
    }

    ~SSRPostProcessEffect() = default;

    bool Init(int width, int height) {
        if (!shader.Init(width, height)) return false;

        shader.Use();
        if (!shader.SetUniform("uColor",      getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logWarning("SSRPostProcessEffect: failed to set uniform 'uColor'");
        if (!shader.SetUniform("uDepth",      getNamedBufferTextureUnit(ENamedBuffer::Depth)))
            logWarning("SSRPostProcessEffect: failed to set uniform 'uDepth'");
        if (!shader.SetUniform("uNormal",     getNamedBufferTextureUnit(ENamedBuffer::Normal)))
            logWarning("SSRPostProcessEffect: failed to set uniform 'uNormal'");
        if (!shader.SetUniform("uPosition",   getNamedBufferTextureUnit(ENamedBuffer::Position)))
            logWarning("SSRPostProcessEffect: failed to set uniform 'uPosition'");
        if (!shader.SetUniform("uAlbedoSpec", getNamedBufferTextureUnit(ENamedBuffer::AlbedoSpec)))
            logWarning("SSRPostProcessEffect: failed to set uniform 'uAlbedoSpec'");
        return true;
    }

    void SetMatrices(const glm::mat4& view, const glm::mat4& projection) {
        viewMatrix = view;
        projMatrix = projection;
    }

    void operator()(GLuint quadVAO, int width, int height) override {
        if (!shader.isEffectEnabled()) return;

        shader.Use();
        if (!shader.SetUniform("uView", viewMatrix))
            logError("SSRPostProcessEffect: failed to set uniform 'uView'");
        if (!shader.SetUniform("uProjection", projMatrix))
            logError("SSRPostProcessEffect: failed to set uniform 'uProjection'");
        if (!shader.SetUniform("uNearPlane", nearPlane))
            logError("SSRPostProcessEffect: failed to set uniform 'uNearPlane'");
        if (!shader.SetUniform("uFarPlane", farPlane))
            logError("SSRPostProcessEffect: failed to set uniform 'uFarPlane'");

        if (!bufferManager.useBuffer(ENamedBuffer::SceneColor, getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logError("SSRPostProcessEffect: failed to bind SceneColor buffer");
        if (!bufferManager.useBuffer(ENamedBuffer::Depth,      getNamedBufferTextureUnit(ENamedBuffer::Depth)))
            logError("SSRPostProcessEffect: failed to bind Depth buffer");
        if (!bufferManager.useBuffer(ENamedBuffer::Normal,     getNamedBufferTextureUnit(ENamedBuffer::Normal)))
            logError("SSRPostProcessEffect: failed to bind Normal buffer");
        if (!bufferManager.useBuffer(ENamedBuffer::Position,   getNamedBufferTextureUnit(ENamedBuffer::Position)))
            logError("SSRPostProcessEffect: failed to bind Position buffer");
        if (!bufferManager.useBuffer(ENamedBuffer::AlbedoSpec, getNamedBufferTextureUnit(ENamedBuffer::AlbedoSpec)))
            logError("SSRPostProcessEffect: failed to bind AlbedoSpec buffer");

        if (!shader.Render(quadVAO))
            logError("SSRPostProcessEffect: render failed");
    }

    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

private:
    PostProcessShader shader;
    const RenderBufferManager& bufferManager;
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projMatrix = glm::mat4(1.0f);
};
