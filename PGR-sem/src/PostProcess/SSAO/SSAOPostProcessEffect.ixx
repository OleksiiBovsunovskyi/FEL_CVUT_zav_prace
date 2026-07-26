module;
#include "pgr.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <vector>;
#include <random>;

export module ssao.postprocesseffect;

import PostProcessEffect;
import postprocessshader;
import RenderBufferManager;
import Logger;

export class SSAOPostProcessEffect : public IPostProcessEffect {
public:
    static constexpr int NUM_SAMPLES = 64;
    
    //Texture units outside BufferManager
    static constexpr int NOISE_UNIT = 12;
    static constexpr int SSAO_UNIT  = 13;  

    SSAOPostProcessEffect(const RenderBufferManager& buffers)
        : ssaoShader(
            "Shaders/PostProcess/SSAO/SSAOVrtx.glsl",
            "Shaders/PostProcess/SSAO/SSAOFrag.glsl"),
          blurShader(
            "Shaders/PostProcess/SSAO/SSAOVrtx.glsl",
            "Shaders/PostProcess/SSAO/SSAOBlurFrag.glsl"),
          compositeShader(
            "Shaders/PostProcess/SSAO/SSAOVrtx.glsl",
            "Shaders/PostProcess/SSAO/SSAOCompFrag.glsl"),
          bufferManager(buffers) {
        generateKernel();
    }

    ~SSAOPostProcessEffect() {
        deleteBuffers();
    }

    bool Init(int width, int height) {
        width_ = width;
        height_ = height;

        if (!ssaoShader.Init(width, height) ||
            !blurShader.Init(width, height) ||
            !compositeShader.Init(width, height)) {
            return false;
        }

        generateNoiseTex();

        ssaoShader.Use();
        if (!ssaoShader.SetUniform("uNoise",    NOISE_UNIT))
            logWarning("SSAOPostProcessEffect: failed to set uniform 'uNoise'");
        if (!ssaoShader.SetUniform("uPosition", getNamedBufferTextureUnit(ENamedBuffer::Position)))
            logWarning("SSAOPostProcessEffect: failed to set uniform 'uPosition'");
        if (!ssaoShader.SetUniform("uNormal",   getNamedBufferTextureUnit(ENamedBuffer::Normal)))
            logWarning("SSAOPostProcessEffect: failed to set uniform 'uNormal'");

        compositeShader.Use();
        if (!compositeShader.SetUniform("uColor", getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logWarning("SSAOPostProcessEffect: failed to set uniform 'uColor'");
        if (!compositeShader.SetUniform("uSSAO",  SSAO_UNIT))
            logWarning("SSAOPostProcessEffect: failed to set uniform 'uSSAO'");

        // SSAO FBO (half res)
        const int halfW = std::max(1, width / 2);
        const int halfH = std::max(1, height / 2);
        glGenFramebuffers(1, &ssaoFBO);
        glGenTextures(1, &ssaoTex);
        glBindTexture(GL_TEXTURE_2D, ssaoTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, halfW, halfH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            logError("SSAOPostProcessEffect: SSAO FBO incomplete");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        // Blur FBO (half res)
        glGenFramebuffers(1, &blurFBO);
        glGenTextures(1, &blurTex);
        glBindTexture(GL_TEXTURE_2D, blurTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, halfW, halfH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, blurFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            logError("SSAOPostProcessEffect: blur FBO incomplete");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    void Resize(int width, int height) {
        deleteBuffers();
        Init(width, height);
    }

    void SetMatrices(const glm::mat4& view, const glm::mat4& projection) {
        viewMatrix = view;
        projMatrix = projection;
    }

    void operator()(GLuint quadVAO, int width, int height) override {
        if (!ssaoShader.isEffectEnabled()) return;

        GLint targetFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &targetFBO);

        glDisable(GL_DEPTH_TEST);

        const int halfW = std::max(1, width / 2);
        const int halfH = std::max(1, height / 2);

        //SASO
        glViewport(0, 0, halfW, halfH);
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        ssaoShader.Use();

        if (!ssaoShader.SetUniform("uRadius", radius))
            logError("SSAOPostProcessEffect: failed to set uniform 'uRadius'");
        if (!ssaoShader.SetUniform("uBias", bias))
            logError("SSAOPostProcessEffect: failed to set uniform 'uBias'");
        if (!ssaoShader.SetUniform("uView", viewMatrix))
            logError("SSAOPostProcessEffect: failed to set uniform 'uView'");
        if (!ssaoShader.SetUniform("uProjection", projMatrix))
            logError("SSAOPostProcessEffect: failed to set uniform 'uProjection'");
        if (!ssaoShader.SetUniform("uNoiseScale", glm::vec2(halfW / 4.0f, halfH / 4.0f)))
            logError("SSAOPostProcessEffect: failed to set uniform 'uNoiseScale'");

        for (int i = 0; i < NUM_SAMPLES; i++) {
            std::string uniName = "uSamples[" + std::to_string(i) + "]";
            if (!ssaoShader.SetUniform(uniName, kernel[i]))
                logWarning("SSAOPostProcessEffect: failed to set uniform '" + uniName + "'");
        }

        glActiveTexture(GL_TEXTURE0 + NOISE_UNIT);
        glBindTexture(GL_TEXTURE_2D, noiseTex);
        if (!bufferManager.useBuffer(ENamedBuffer::Position, getNamedBufferTextureUnit(ENamedBuffer::Position)))
            logError("SSAOPostProcessEffect: failed to bind Position buffer");
        if (!bufferManager.useBuffer(ENamedBuffer::Normal, getNamedBufferTextureUnit(ENamedBuffer::Normal)))
            logError("SSAOPostProcessEffect: failed to bind Normal buffer");
        if (!ssaoShader.Render(quadVAO))
            logError("SSAOPostProcessEffect: SSAO pass render failed");

        // Blur
        glBindFramebuffer(GL_FRAMEBUFFER, blurFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        blurShader.Use();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ssaoTex);
        if (!blurShader.Render(quadVAO))
            logError("SSAOPostProcessEffect: blur pass render failed");

        // Composite
        glViewport(0, 0, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        compositeShader.Use();

        if (!compositeShader.SetUniform("uStrength", strength))
            logError("SSAOPostProcessEffect: failed to set uniform 'uStrength'");

        if (!bufferManager.useBuffer(ENamedBuffer::SceneColor, getNamedBufferTextureUnit(ENamedBuffer::SceneColor)))
            logError("SSAOPostProcessEffect: failed to bind SceneColor buffer");
        glActiveTexture(GL_TEXTURE0 + SSAO_UNIT);
        glBindTexture(GL_TEXTURE_2D, blurTex);
        if (!compositeShader.Render(quadVAO))
            logError("SSAOPostProcessEffect: composite pass render failed");
    }

    float radius = 1.5f;
    float bias = 0.025f;
    float strength = 1.5f;

private:
    void generateKernel() {
        std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
        std::default_random_engine gen(12345u);

        kernel.clear();
        kernel.reserve(NUM_SAMPLES);

        for (int i = 0; i < NUM_SAMPLES; ++i) {
            glm::vec3 s(rnd(gen) * 2.0f - 1.0f,
                        rnd(gen) * 2.0f - 1.0f,
                        rnd(gen));
            s = glm::normalize(s) * rnd(gen);

            float scale = float(i) / float(NUM_SAMPLES);
            scale = glm::mix(0.1f, 1.0f, scale * scale);
            kernel.push_back(s * scale);
        }
    }

    void generateNoiseTex() {
        if (noiseTex) { glDeleteTextures(1, &noiseTex); noiseTex = 0; }

        std::uniform_real_distribution<float> rnd(0.0f, 1.0f);
        std::default_random_engine gen(98765u);

        glm::vec3 noise[16];
        for (int i = 0; i < 16; ++i)
            noise[i] = glm::vec3(rnd(gen) * 2.0f - 1.0f,
                                 rnd(gen) * 2.0f - 1.0f,
                                 0.0f);

        glGenTextures(1, &noiseTex);
        glBindTexture(GL_TEXTURE_2D, noiseTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void deleteBuffers() {
        if (ssaoTex) { glDeleteTextures(1, &ssaoTex); ssaoTex = 0; }
        if (ssaoFBO) { glDeleteFramebuffers(1, &ssaoFBO); ssaoFBO = 0; }
        if (blurTex) { glDeleteTextures(1, &blurTex); blurTex = 0; }
        if (blurFBO) { glDeleteFramebuffers(1, &blurFBO); blurFBO = 0; }
        if (noiseTex) { glDeleteTextures(1, &noiseTex); noiseTex = 0; }
    }

    PostProcessShader ssaoShader;
    PostProcessShader blurShader;
    PostProcessShader compositeShader;

    GLuint ssaoFBO = 0, ssaoTex = 0;
    GLuint blurFBO = 0, blurTex = 0;
    GLuint noiseTex = 0;
    const RenderBufferManager& bufferManager;
    std::vector<glm::vec3> kernel;
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projMatrix = glm::mat4(1.0f);
    int width_ = 0, height_ = 0;
};
