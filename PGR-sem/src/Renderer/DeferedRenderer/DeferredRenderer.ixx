module;
#include "pgr.h"
#include <vector>
#include <glm/glm.hpp>
export module deferredrenderer;

import Model;
import Camera;
import IRenderer;
import RenderBufferManager;
import LightSource;



export class DeferredRenderer : public IRenderer {
    RenderBufferManager bufferManager;

    glm::mat4 prevVP{1.0f};
    bool firstFrame = true;

    GLuint geometryShader = 0;
    GLuint lightingShader = 0;
    GLuint shadowShader   = 0;

    GLint uMVP     = -1;
    GLint uPrevMVP = -1;
    GLint uModel   = -1;

    static constexpr int SHADOW_MAP_SIZE  = 2048;
    static constexpr int MAX_SHADOW_LIGHTS = 4;
    
    GLuint shadowFBOs[MAX_SHADOW_LIGHTS]          = {};
    GLuint shadowDepthTextures[MAX_SHADOW_LIGHTS] = {};
    ShadowSlotGPU shadowSlots[MAX_SHADOW_LIGHTS]  = {};
    int    numShadowLights = 0;

    GLint  uShadowLightMVP = -1;  

    GLuint sceneLightUBO = 0;

    GLuint quadVAO = 0;
    GLuint quadVBO = 0;

    void initShadowMaps();
    void deleteShadowMaps();
    void buildShadowMatrix(glm::mat4& mat, const LightSource* l,
                           glm::vec3 pos, glm::vec3 target, bool ortho,
                           float orthoSize, float fov);

    // Skybox
    GLuint skyboxVAO     = 0;
    GLuint skyboxVBO     = 0;
    GLuint skyboxShader  = 0;
    GLuint skyboxCubemap = 0;
    float  skyboxMaxLod  = 8.0f;

    
    GLuint gBufferFBO    = 0;
    GLuint sceneColorFBO = 0;

    int width = 0, height = 0;

    bool initBuffers(int w, int h);
    void deleteBuffers();

public:
   
    float shadowBiasMin = 0.00005f;
    float shadowBiasMax = 0.0002f; 
    float iblDiffuseScale = 0.3f;

    bool  loadSkybox(const char* dir);
    GLuint getSkyboxCubemap() const { return skyboxCubemap; }
    float  getSkyboxMaxLod()  const { return skyboxMaxLod; }

    bool init(int width, int height) override;
    void render(const std::vector<Model*>& meshes,
                const LightEnvironment&    lights,
                const Camera&              camera) override;
    void resize(int width, int height) override;

    const RenderBufferManager& getBufferManager() const { return bufferManager; }
          RenderBufferManager& getBufferManager()       { return bufferManager; }


    ~DeferredRenderer() override;
};
