module;
#include "pgr.h"
#include <algorithm>
#include <vector>
#include <iostream>

module postprocess.renderer;

bool PostProcessRenderer::init(int w, int h, RenderBufferManager& buffers) {
    bufferManager = &buffers;
    width = w; height = h;

    constexpr float quadVerts[] = {
        -1.0f, -1.0f,  0.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  0.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  0.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  0.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  0.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f,  0.0f, 1.0f,
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    return blitShader.init();
}

[[nodiscard]] bool PostProcessRenderer::resize(int w, int h) {
    width = w; height = h;
    return true;
}

void PostProcessRenderer::addEffect(unsigned int order, std::shared_ptr<IPostProcessEffect> effect) {
    if (!effect->Init(width, height))
        std::cerr << "PostProcessRenderer: effect Init() failed for slot " << order << "\n";
    effects[order] = std::move(effect);
}

void PostProcessRenderer::removeEffect(unsigned int order) {
    effects.erase(order);
}

void PostProcessRenderer::render(GLuint sceneColorTex) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (effects.empty()) {
        blitShader.blit(sceneColorTex, quadVAO);
        return;
    }

    glDisable(GL_DEPTH_TEST);

    std::vector<unsigned int> keys;
    keys.reserve(effects.size());
    for (auto& [k, _] : effects)
        keys.push_back(k);
    std::sort(keys.begin(), keys.end());

    for (size_t i = 0; i < keys.size(); i++) {
        if (!bufferManager->setOutput(ENamedBuffer::Blit)) {
            std::cerr << "PostProcessRenderer: setOutput(Blit) failed at step " << i << "\n";
            break;
        }
        glClear(GL_COLOR_BUFFER_BIT);

        (*effects.at(keys[i]))(quadVAO, width, height);
        
        if (!bufferManager->copyBuffer(ENamedBuffer::Blit, ENamedBuffer::SceneColor, blitShader, quadVAO)) {
            std::cerr << "PostProcessRenderer: copyBuffer failed at step " << i << "\n";
            break;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    blitShader.blit(bufferManager->getHandle(ENamedBuffer::SceneColor), quadVAO);

    glEnable(GL_DEPTH_TEST);
}

PostProcessRenderer::~PostProcessRenderer() {
    if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO) glDeleteBuffers(1, &quadVBO);
}
