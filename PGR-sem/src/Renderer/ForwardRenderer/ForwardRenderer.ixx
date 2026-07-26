module;
#include "pgr.h"
#include <vector>
export module forwardrenderer;

import Model;
import Camera;
import IRenderer;


export class ForwardRenderer : public IRenderer {
    GLuint shader        = 0;
    GLuint sceneLightUBO = 0;
    GLuint targetFBO     = 0;
    GLuint skyboxCubemap = 0;
    float  skyboxMaxLod  = 8.0f;

public:
    bool init(int width, int height) override;
    void render(const std::vector<Model*>& meshes,
                        const LightEnvironment& lights,
                        const Camera& camera)override;
    void resize(int width, int height) override {}

    void setTargetFBO(GLuint fbo) { targetFBO = fbo; }
    void setSkybox(GLuint cubemap, float maxLod) { skyboxCubemap = cubemap; skyboxMaxLod = maxLod; }

    ~ForwardRenderer() override;
};
