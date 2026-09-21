module;
#include "pgr.h"

export module tonemappingPPE;

import PostProcessEffect;
import postprocessshader;
import RenderBufferManager;
import Logger.gl;
export class ToneMappingPostProcessEffect : public IPostProcessEffect {
public:
    ToneMappingPostProcessEffect(const RenderBufferManager& buffers)
        : shader(
            "Shaders/PostProcess/ToneMapping/tonemapVrtx.glsl",
            "Shaders/PostProcess/ToneMapping/tonemapFrag.glsl"),
          bufferManager(buffers) {
    }  
    
    ~ToneMappingPostProcessEffect() = default;

    [[nodiscard]] bool Init(const int width, const int height) override {
        if (!shader.Init(width, height)) return false;
        shader.Use();
        if (!shader.SetUniform("uScene", getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logWarning("ToneMappingPostProcessEffect: failed to set uniform 'uScene'");
        return true;
    }

    void operator()(GLuint quadVAO, int width, int height) override {
        shader.Use();   
        if (!bufferManager.useBuffer(ENamedBuffer::SceneColor, getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logError("ToneMappingPostProcessEffect: failed to bind SceneColor buffer");
        if (!shader.SetUniform("uExposure", exposure))
            logError("ToneMappingPostProcessEffect: failed to set uniform 'uExposure'");
        if (!shader.SetUniform("uGamma", gamma))
            logError("ToneMappingPostProcessEffect: failed to set uniform 'uGamma'");
        if (!shader.Render(quadVAO))
            logError("ToneMappingPostProcessEffect: render failed");
    }

    float exposure = 1.0f;
    float gamma = 2.2f;

private:
    PostProcessShader shader;
    const RenderBufferManager& bufferManager;
};
