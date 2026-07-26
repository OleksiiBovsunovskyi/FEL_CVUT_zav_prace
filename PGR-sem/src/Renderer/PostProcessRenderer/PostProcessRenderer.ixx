module;

#include "pgr.h"
#include <memory>
#include <unordered_map>

export module postprocess.renderer;

import IRenderer;
import PostProcessEffect;
import RenderBufferManager;
import BlitShader;

export class PostProcessRenderer {
private:
    GLuint quadVAO = 0, quadVBO = 0;

    BlitShader blitShader;

    RenderBufferManager* bufferManager = nullptr;

    int width = 0, height = 0;

    std::unordered_map<unsigned int, std::shared_ptr<IPostProcessEffect>> effects;


public:
    [[nodiscard]] bool init(int w, int h, RenderBufferManager& buffers);
    [[nodiscard]] bool resize(int w, int h);

    void addEffect(unsigned int order, std::shared_ptr<IPostProcessEffect> effect);
    void removeEffect(unsigned int order);

    void render(GLuint sceneColorTex);

    ~PostProcessRenderer();
};
