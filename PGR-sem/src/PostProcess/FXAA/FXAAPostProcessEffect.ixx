module;
#include "pgr.h"
#include <glm/glm.hpp>

export module fxaa.postprocesseffect;

import PostProcessEffect;
import postprocessshader;
import RenderBufferManager;
import Logger.gl;

export class FXAAPostProcessEffect : public IPostProcessEffect {
public:
    FXAAPostProcessEffect(const RenderBufferManager& buffers)
        : shader(
            "Shaders/PostProcess/FXAA/fxaaVrtx.glsl",
            "Shaders/PostProcess/FXAA/fxaaFrag.glsl"),
          bufferManager(buffers) {
    }

    ~FXAAPostProcessEffect() = default;

    [[nodiscard]] bool Init(int width, int height) override {
        width_  = width;
        height_ = height;
        if (!shader.Init(width, height)) return false;
        shader.Use();
        if (!shader.SetUniform("uScene", getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logWarning("FXAAPostProcessEffect: failed to set uniform 'uScene'");
        return true;
    }

    void Resize(int width, int height) {
        width_  = width;
        height_ = height;
    }

    void operator()(GLuint quadVAO, int width, int height) override {
        shader.Use();
        if (!bufferManager.useBuffer(ENamedBuffer::SceneColor, getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logError("FXAAPostProcessEffect: failed to bind SceneColor buffer");
        if (!shader.SetUniform("uTexelSize", glm::vec2(1.0f / width, 1.0f / height)))
            logError("FXAAPostProcessEffect: failed to set uniform 'uTexelSize'");
        if (!shader.SetUniform("uSubpixelAA", subpixelAA))
            logError("FXAAPostProcessEffect: failed to set uniform 'uSubpixelAA'");
        if (!shader.SetUniform("uEdgeThreshold", edgeThreshold))
            logError("FXAAPostProcessEffect: failed to set uniform 'uEdgeThreshold'");
        if (!shader.SetUniform("uEdgeThresholdMin", edgeThresholdMin))
            logError("FXAAPostProcessEffect: failed to set uniform 'uEdgeThresholdMin'");
        if (!shader.Render(quadVAO))
            logError("FXAAPostProcessEffect: render failed");
    }

    float subpixelAA        = 0.75f;
    float edgeThreshold     = 0.166f;
    float edgeThresholdMin  = 0.0833f;

private:
    PostProcessShader shader;
    const RenderBufferManager& bufferManager;
    int width_  = 0;
    int height_ = 0;
};
