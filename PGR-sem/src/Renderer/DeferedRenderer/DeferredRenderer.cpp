module;
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include "pgr.h"
#include <cmath>
#include <IL/il.h>

module deferredrenderer;
import Model;
import Camera;
import LightSource;
import ShaderLoader;
import Logger;

bool DeferredRenderer::loadSkybox(const char* dir) {
    static const char* names[]   = { "px", "nx", "py", "ny", "pz", "nz" };
    static const GLenum targets[] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
    };

    if (skyboxCubemap) { glDeleteTextures(1, &skyboxCubemap); skyboxCubemap = 0; }
    glGenTextures(1, &skyboxCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCubemap);

    for (int i = 0; i < 6; i++) {
        std::string path = std::string(dir) + "/" + names[i] + ".png";
        ILuint img;
        ilGenImages(1, &img);
        ilBindImage(img);
        if (!ilLoadImage(path.c_str())) {
            std::cerr << "Skybox: failed to load '" << path << "'\n";
            ilDeleteImages(1, &img);
            glDeleteTextures(1, &skyboxCubemap);
            skyboxCubemap = 0;
            return false;
        }
        ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
        glTexImage2D(targets[i], 0, GL_RGBA,
                     ilGetInteger(IL_IMAGE_WIDTH), ilGetInteger(IL_IMAGE_HEIGHT),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, ilGetData());
        ilDeleteImages(1, &img);
    }

    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    //max lod level
    GLint faceSize = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &faceSize);
    skyboxMaxLod = faceSize > 0 ? std::floor(std::log2(static_cast<float>(faceSize))) : 8.0f;

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return true;
}

// Render targets
 
bool DeferredRenderer::initBuffers(int w, int h) {
    deleteBuffers();
    width = w; height = h;

    //!RENDERTARGETS 
    bufferManager.declare(ENamedBuffer::Position,   "Position",   BufferDesc::rgb32F());
    bufferManager.declare(ENamedBuffer::Normal,     "Normal",     BufferDesc::rgba16F());
    bufferManager.declare(ENamedBuffer::AlbedoSpec, "AlbedoSpec", BufferDesc::rgba8());
    bufferManager.declare(ENamedBuffer::MotionVec,  "MotionVec",  BufferDesc::rg16F());
    bufferManager.declare(ENamedBuffer::MetallicAO, "MetallicAO", BufferDesc::rg8());
    bufferManager.declare(ENamedBuffer::Emissive,   "Emissive",   BufferDesc::rgb16F());
    bufferManager.declare(ENamedBuffer::Depth,      "Depth",      BufferDesc::depth24());
    bufferManager.declare(ENamedBuffer::SceneColor, "SceneColor", BufferDesc::rgba16F(GL_LINEAR));
    bufferManager.declare(ENamedBuffer::Blit,       "Blit",       BufferDesc::rgba16F(GL_LINEAR));

    if (!bufferManager.init(w, h)) return false;

    //!G-BUFFER
    gBufferFBO = bufferManager.buildFramebuffer({
        {ENamedBuffer::Position,   GL_COLOR_ATTACHMENT0},
        {ENamedBuffer::Normal,     GL_COLOR_ATTACHMENT1},
        {ENamedBuffer::AlbedoSpec, GL_COLOR_ATTACHMENT2},
        {ENamedBuffer::MotionVec,  GL_COLOR_ATTACHMENT3},
        {ENamedBuffer::MetallicAO, GL_COLOR_ATTACHMENT4},
        {ENamedBuffer::Emissive,   GL_COLOR_ATTACHMENT5},
    }, ENamedBuffer::Depth);
    if (!gBufferFBO) return false;
        
    //!SCENE COLOR
    sceneColorFBO = bufferManager.buildFramebuffer({
        {ENamedBuffer::SceneColor, GL_COLOR_ATTACHMENT0},
    }, ENamedBuffer::Depth);
    if (!sceneColorFBO) return false;

    return true;
}

void DeferredRenderer::deleteBuffers() {
    if (sceneColorFBO) { glDeleteFramebuffers(1, &sceneColorFBO); sceneColorFBO = 0; }
    if (gBufferFBO)    { glDeleteFramebuffers(1, &gBufferFBO);    gBufferFBO    = 0; }
    bufferManager.shutdown();
}

void DeferredRenderer::initShadowMaps() {
    glGenTextures(MAX_SHADOW_LIGHTS, shadowDepthTextures);
    glGenFramebuffers(MAX_SHADOW_LIGHTS, shadowFBOs);
    for (int i = 0; i < MAX_SHADOW_LIGHTS; i++) {
        glBindTexture(GL_TEXTURE_2D, shadowDepthTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                     SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        constexpr float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBOs[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, shadowDepthTextures[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DeferredRenderer::deleteShadowMaps() {
    glDeleteFramebuffers(MAX_SHADOW_LIGHTS, shadowFBOs);
    glDeleteTextures(MAX_SHADOW_LIGHTS, shadowDepthTextures);
    for (int i = 0; i < MAX_SHADOW_LIGHTS; i++) {
        shadowFBOs[i] = 0;
        shadowDepthTextures[i] = 0;
    }
}

// Init, cleanup

bool DeferredRenderer::init(int w, int h) {
    const GLuint geoShaders[] = {
        ShaderLoader::createShader(GL_VERTEX_SHADER,   "Shaders/GBuffer/gBufferVrtx.glsl"),
        ShaderLoader::createShader(GL_FRAGMENT_SHADER, "Shaders/GBuffer/gBufferFrag.glsl"),
        0
    };
    geometryShader = pgr::createProgram(geoShaders);
    if (!geometryShader) {
        std::cerr << "DeferredRenderer: failed to create geometry shader\n";
        return false;
    }
    uMVP     = glGetUniformLocation(geometryShader, "uMVP");
    uPrevMVP = glGetUniformLocation(geometryShader, "uPrevMVP");
    uModel   = glGetUniformLocation(geometryShader, "uModel");

    const GLuint litShaders[] = {
        ShaderLoader::createShader(GL_VERTEX_SHADER,   "Shaders/Lighting/lightingVrtx.glsl"),
        ShaderLoader::createShader(GL_FRAGMENT_SHADER, "Shaders/Lighting/lightingFrag.glsl"),
        0
    };
    lightingShader = pgr::createProgram(litShaders);
    if (!lightingShader) {
        std::cerr << "DeferredRenderer: failed to create lighting shader\n";
        return false;
    }

    const GLuint shadowShaders[] = {
        ShaderLoader::createShader(GL_VERTEX_SHADER,   "Shaders/GBuffer/shadowVrtx.glsl"),
        ShaderLoader::createShader(GL_FRAGMENT_SHADER, "Shaders/GBuffer/shadowFrag.glsl"),
        0
    };
    shadowShader = pgr::createProgram(shadowShaders);
    if (!shadowShader) {
        std::cerr << "DeferredRenderer: failed to create shadow shader\n";
        return false;
    }

    // Fullscreen quad 
    constexpr float quadVerts[] = {
        -1.0f, -1.0f,    1.0f, -1.0f,    1.0f,  1.0f,
        -1.0f, -1.0f,    1.0f,  1.0f,   -1.0f,  1.0f,
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);


    // G-buffer shader
    glUseProgram(geometryShader);
    {
        GLuint blk = glGetUniformBlockIndex(geometryShader, "MaterialBlock");
        if (blk != GL_INVALID_INDEX) glUniformBlockBinding(geometryShader, blk, 1);
        glUniform1i(glGetUniformLocation(geometryShader, "uAlbedoTexture"),   0);
        glUniform1i(glGetUniformLocation(geometryShader, "uNormalTexture"),   1);
        glUniform1i(glGetUniformLocation(geometryShader, "uORMTexture"),      2);
        glUniform1i(glGetUniformLocation(geometryShader, "uEmissiveTexture"), 3);
    }

    uShadowLightMVP = glGetUniformLocation(shadowShader, "uLightMVP");

    glUseProgram(lightingShader);
    glUniform1i(glGetUniformLocation(lightingShader, "gPosition"),   0);
    glUniform1i(glGetUniformLocation(lightingShader, "gNormal"),     1);
    glUniform1i(glGetUniformLocation(lightingShader, "gAlbedoSpec"), 2);
    glUniform1i(glGetUniformLocation(lightingShader, "uDepthMap"),   3);
    glUniform1i(glGetUniformLocation(lightingShader, "gMetallicAO"), 4);
    glUniform1i(glGetUniformLocation(lightingShader, "gEmissive"),   5);
    glUniform1i(glGetUniformLocation(lightingShader, "uSkybox"),     6);
    {
        // Shadow maps (units 7-10)
        int shadowUnits[MAX_SHADOW_LIGHTS] = {7, 8, 9, 10};
        glUniform1iv(glGetUniformLocation(lightingShader, "uShadowMaps"),
                     MAX_SHADOW_LIGHTS, shadowUnits);
    }

    glTexParameterf(GL_TEXTURE_2D, 0x84FE, 16.0f);

    initShadowMaps();

    if (!initBuffers(w, h)) return false;

    glGenBuffers(1, &sceneLightUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, sceneLightUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(SceneLightUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Bind UBO
    GLuint blockIdx = glGetUniformBlockIndex(lightingShader, "SceneLightBlock");
    if (blockIdx != GL_INVALID_INDEX)
        glUniformBlockBinding(lightingShader, blockIdx, 0);

    // Skybox cube
    static constexpr float skyboxVerts[] = {
        -1, 1,-1,  -1,-1,-1,   1,-1,-1,   1,-1,-1,   1, 1,-1,  -1, 1,-1,
        -1,-1, 1,  -1,-1,-1,  -1, 1,-1,  -1, 1,-1,  -1, 1, 1,  -1,-1, 1,
         1,-1,-1,   1,-1, 1,   1, 1, 1,   1, 1, 1,   1, 1,-1,   1,-1,-1,
        -1,-1, 1,  -1, 1, 1,   1, 1, 1,   1, 1, 1,   1,-1, 1,  -1,-1, 1,
        -1, 1,-1,   1, 1,-1,   1, 1, 1,   1, 1, 1,  -1, 1, 1,  -1, 1,-1,
        -1,-1,-1,  -1,-1, 1,   1,-1,-1,   1,-1,-1,  -1,-1, 1,   1,-1, 1,
    };
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVerts), skyboxVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    const GLuint skyShaders[] = {
        ShaderLoader::createShader(GL_VERTEX_SHADER,   "Shaders/Skybox/skyboxVrtx.glsl"),
        ShaderLoader::createShader(GL_FRAGMENT_SHADER, "Shaders/Skybox/skyboxFrag.glsl"),
        0
    };
    skyboxShader = pgr::createProgram(skyShaders);
    if (skyboxShader) {
        glUseProgram(skyboxShader);
        glUniform1i(glGetUniformLocation(skyboxShader, "uSkybox"), 0);
    }

    return true;
}

void DeferredRenderer::resize(int w, int h) {
    width = w; height = h;
    bufferManager.resize(w, h);
}

DeferredRenderer::~DeferredRenderer() {
    if (sceneLightUBO) glDeleteBuffers(1, &sceneLightUBO);
    deleteShadowMaps();
    if (quadVAO)        glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO)        glDeleteBuffers(1, &quadVBO);
    if (skyboxVAO)      glDeleteVertexArrays(1, &skyboxVAO);
    if (skyboxVBO)      glDeleteBuffers(1, &skyboxVBO);
    if (skyboxCubemap)  glDeleteTextures(1, &skyboxCubemap);
    if (skyboxShader)   pgr::deleteProgramAndShaders(skyboxShader);
    if (geometryShader) pgr::deleteProgramAndShaders(geometryShader);
    if (lightingShader) pgr::deleteProgramAndShaders(lightingShader);
    if (shadowShader)   pgr::deleteProgramAndShaders(shadowShader);
    deleteBuffers();   
}

void DeferredRenderer::buildShadowMatrix(glm::mat4& mat, const LightSource* l,
                                         glm::vec3 pos, glm::vec3 target, bool ortho,
                                         float orthoSize, float fov) {
    glm::vec3 dir = glm::normalize(target - pos);
    glm::vec3 up  = (std::abs(dir.y) > 0.999f)
                  ? glm::vec3(0.0f, 0.0f, -1.0f)
                  : glm::vec3(0.0f, 1.0f,  0.0f);
    glm::mat4 view = glm::lookAt(pos, target, up);
    glm::mat4 proj = ortho
        ? glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize,
                     l->getShadowNear(), l->getShadowFar())
        : glm::perspective(glm::radians(fov), 1.0f,
                           l->getShadowNear(), l->getShadowFar());
    mat = proj * view;
}

// Renderer

void DeferredRenderer::render(const std::vector<Model*>& meshes,
                               const LightEnvironment&    lights,
                               const Camera&              camera) {

    //Shadows
    numShadowLights = 0;

    auto collectShadow = [&](int lightType, int lightIndex,
                              glm::vec3 pos, glm::vec3 target,
                              bool ortho, float orthoSize, float fov,
                              const LightSource* l) {
        if (numShadowLights >= MAX_SHADOW_LIGHTS) return;
        auto& slot = shadowSlots[numShadowLights];
        buildShadowMatrix(slot.lightSpaceMatrix, l, pos, target, ortho, orthoSize, fov);
        slot.lightType  = lightType;
        slot.lightIndex = lightIndex;
        numShadowLights++;
    };

    for (int i = 0; i < static_cast<int>(lights.pointLights.size()); i++) {
        auto* l = lights.pointLights[i];
        if (!l->getCastsShadow()) continue;
        collectShadow(0, i, l->getPosition(), l->getShadowTarget(),
                      false, 0.0f, l->getShadowFov(), l);
    }
    for (int i = 0; i < static_cast<int>(lights.dirLights.size()); i++) {
        auto* l = lights.dirLights[i];
        if (!l->getCastsShadow()) continue;
        glm::vec3 center = l->getShadowTarget();
        glm::vec3 lpos   = center - glm::normalize(l->getDirection()) * l->getShadowFar() * 0.5f;
        collectShadow(1, i, lpos, center, true, l->getShadowOrthoSize(), 0.0f, l);
    }
    for (int i = 0; i < static_cast<int>(lights.spotLights.size()); i++) {
        auto* l = lights.spotLights[i];
        if (!l->getCastsShadow()) continue;
        glm::vec3 target = l->getPosition() + l->getDirection();
        collectShadow(2, i, l->getPosition(), target,
                      false, 0.0f, l->getShadowFov(), l);
    }

    if (numShadowLights > 0 && !meshes.empty()) {
        glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glCullFace(GL_FRONT);
        glUseProgram(shadowShader);

        for (int s = 0; s < numShadowLights; s++) {
            glBindFramebuffer(GL_FRAMEBUFFER, shadowFBOs[s]);
            glClear(GL_DEPTH_BUFFER_BIT);
            for (auto* mesh : meshes) {
                glm::mat4 mvp = shadowSlots[s].lightSpaceMatrix * mesh->getModelMatrix();
                glUniformMatrix4fv(uShadowLightMVP, 1, GL_FALSE, glm::value_ptr(mvp));
                mesh->drawGeometry();
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glCullFace(GL_BACK);
    }

    //!Geometry pass
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(geometryShader);

    glm::mat4 VP = camera.getProjection() * camera.getView();

    if (firstFrame) {
        prevVP     = VP;
        firstFrame = false;
    }

    //cull and sort by material
    std::vector<Model*> visible;
    visible.reserve(meshes.size());
    for (auto* mesh : meshes)
        if (camera.isInsideFrustum(mesh))
            visible.push_back(mesh);
    std::sort(visible.begin(), visible.end(), [](const Model* a, const Model* b) {
        return &a->getMaterial() < &b->getMaterial();
    });

    const Material* lastMaterial = nullptr;
    for (auto* mesh : visible) {
        glm::mat4 model     = mesh->getModelMatrix();
        glm::mat4 prevModel = mesh->getPrevModelMatrix();
        glm::mat4 mvp       = VP * model;
        glm::mat4 prevMVP   = prevVP     * prevModel;

        glCullFace(GL_BACK);
        glUniformMatrix4fv(uMVP,     1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(uPrevMVP, 1, GL_FALSE, glm::value_ptr(prevMVP));
        glUniformMatrix4fv(uModel,   1, GL_FALSE, glm::value_ptr(model));
        if (&mesh->getMaterial() != lastMaterial) {
            lastMaterial = &mesh->getMaterial();
            mesh->getMaterial().bindUniforms(geometryShader);
        }
        mesh->drawGeometry();
        mesh->advancePrevModelMatrix();
    }

    prevVP = VP;

    //Lights
    glBindFramebuffer(GL_FRAMEBUFFER, sceneColorFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(lightingShader);
    if (!bufferManager.useBuffer(ENamedBuffer::Position,   0)) return;
    if (!bufferManager.useBuffer(ENamedBuffer::Normal,     1)) return;
    if (!bufferManager.useBuffer(ENamedBuffer::AlbedoSpec, 2)) return;
    if (!bufferManager.useBuffer(ENamedBuffer::Depth,      3)) return;
    if (!bufferManager.useBuffer(ENamedBuffer::MetallicAO, 4)) return;
    if (!bufferManager.useBuffer(ENamedBuffer::Emissive,   5)) return;
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCubemap);
    for (int s = 0; s < MAX_SHADOW_LIGHTS; s++) {
        glActiveTexture(GL_TEXTURE7 + s);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTextures[s]);
    }

    //Prepare scene light UBO
    SceneLightUBO uboData{};
    uboData.cameraPos        = camera.getPosition();
    uboData.skyboxMaxLod     = skyboxMaxLod;
    uboData.ambient          = lights.ambient;
    uboData.shadowBiasMin    = shadowBiasMin;
    uboData.shadowBiasMax    = shadowBiasMax;
    uboData.hasSkybox        = skyboxCubemap ? 1 : 0;
    uboData.iblDiffuseScale  = iblDiffuseScale;
    uboData.numShadowLights  = numShadowLights;
    for (int s = 0; s < numShadowLights; s++)
        uboData.shadowSlots[s] = shadowSlots[s];

    uboData.numPointLights = std::min((int)lights.pointLights.size(), MAX_POINT_LIGHTS);
    for (int i = 0; i < uboData.numPointLights; i++) {
        const auto* l = lights.pointLights[i];
        uboData.pointLights[i] = { l->getPosition(), 0.f, l->getColor(), l->getConstant(),
                                   l->getLinear(), l->getQuadratic(), {0.f, 0.f} };
    }

    uboData.numDirLights = std::min((int)lights.dirLights.size(), MAX_DIR_LIGHTS);
    for (int i = 0; i < uboData.numDirLights; i++) {
        const auto* l = lights.dirLights[i];
        uboData.dirLights[i] = { l->getDirection(), 0.f, l->getColor(), 0.f };
    }

    uboData.numSpotLights = std::min((int)lights.spotLights.size(), MAX_SPOT_LIGHTS);
    for (int i = 0; i < uboData.numSpotLights; i++) {
        const auto* l = lights.spotLights[i];
        uboData.spotLights[i] = { l->getPosition(), 0.f, l->getDirection(), 0.f,
                                   l->getColor(), l->getConstant(), l->getLinear(),
                                   l->getQuadratic(),
                                   std::cos(glm::radians(l->getInnerCutoff())),
                                   std::cos(glm::radians(l->getOuterCutoff())) };
    }

    glBindBuffer(GL_UNIFORM_BUFFER, sceneLightUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SceneLightUBO), &uboData);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, sceneLightUBO);

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    //Skybox
    if (skyboxCubemap && skyboxShader) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);

        glUseProgram(skyboxShader);
        
        glm::mat4 view = glm::mat4(glm::mat3(camera.getView()));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "uView"),       1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "uProjection"), 1, GL_FALSE, glm::value_ptr(camera.getProjection()));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCubemap);

        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    glEnable(GL_DEPTH_TEST);
}
