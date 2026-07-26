module;
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <cmath>
#include "pgr.h"

module forwardrenderer;
import IRenderer;
import Model;
import Camera;
import LightSource;
import ShaderLoader;


bool ForwardRenderer::init(int /*width*/, int /*height*/) {
    GLuint shaders[] = {
        ShaderLoader::createShader(GL_VERTEX_SHADER,   "Shaders/Forward/forwardVrtx.glsl"),
        ShaderLoader::createShader(GL_FRAGMENT_SHADER, "Shaders/Forward/forwardFrag.glsl"),
        0
    };
    shader = pgr::createProgram(shaders);
    if (!shader) {
        std::cerr << "ForwardRenderer: failed to create shader\n";
        return false;
    }

    glGenBuffers(1, &sceneLightUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, sceneLightUBO);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(SceneLightUBO), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    GLuint blockIdx = glGetUniformBlockIndex(shader, "SceneLightBlock");
    if (blockIdx != GL_INVALID_INDEX)
        glUniformBlockBinding(shader, blockIdx, 0);

    GLuint matBlk = glGetUniformBlockIndex(shader, "MaterialBlock");
    if (matBlk != GL_INVALID_INDEX)
        glUniformBlockBinding(shader, matBlk, 1);

    glUseProgram(shader);
    glUniform1i(glGetUniformLocation(shader, "uAlbedoTexture"),   0);
    glUniform1i(glGetUniformLocation(shader, "uNormalTexture"),   1);
    glUniform1i(glGetUniformLocation(shader, "uORMTexture"),      2);
    glUniform1i(glGetUniformLocation(shader, "uEmissiveTexture"), 3);

    return true;
}

ForwardRenderer::~ForwardRenderer() {
    if (sceneLightUBO) glDeleteBuffers(1, &sceneLightUBO);
    if (shader) pgr::deleteProgramAndShaders(shader);
}
void ForwardRenderer::render(const std::vector<Model*>& meshes,
                              const LightEnvironment&    lights,
                              const Camera&              camera) {
    
    if (meshes.empty()) return;

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(shader);
    glm::mat4 vp     = camera.getProjection() * camera.getView();
    glm::vec3 camPos = camera.getPosition();

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCubemap);
    glUniform1i(glGetUniformLocation(shader, "uSkybox"), 8);

    SceneLightUBO uboData{};
    uboData.cameraPos       = camPos;
    uboData.skyboxMaxLod    = skyboxMaxLod;
    uboData.ambient         = lights.ambient;
    uboData.hasSkybox       = skyboxCubemap ? 1 : 0;
    uboData.iblDiffuseScale = 0.3f;
    uboData.numShadowLights = 0;

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

    // opaque + cutout
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    std::vector<Model*> opaque;
    for (auto* mesh : meshes)
        if (!mesh->getMaterial().isTransparent())
            opaque.push_back(mesh);
    std::sort(opaque.begin(), opaque.end(), [](const Model* a, const Model* b) {
        return &a->getMaterial() < &b->getMaterial();
    });

    for (auto* mesh : opaque) {
        glm::mat4 model = mesh->getModelMatrix();
        glUniformMatrix4fv(glGetUniformLocation(shader, "uMVP"),   1, GL_FALSE, glm::value_ptr(vp * model));
        glUniformMatrix4fv(glGetUniformLocation(shader, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        mesh->getMaterial().bindUniforms(shader);
        mesh->drawGeometry();
    }

    //transparent
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    std::vector<Model*> transparent;
    for (auto* mesh : meshes)
        if ( mesh->getMaterial().isTransparent())
            transparent.push_back(mesh);

    std::sort(transparent.begin(), transparent.end(), [&camPos](const Model* a, const Model* b) {
        glm::vec3 pa = glm::vec3(a->getModelMatrix()[3]);
        glm::vec3 pb = glm::vec3(b->getModelMatrix()[3]);
        return glm::dot(pa - camPos, pa - camPos) > glm::dot(pb - camPos, pb - camPos);
    });

    for (auto* mesh : transparent) {
        glm::mat4 model = mesh->getModelMatrix();
        glUniformMatrix4fv(glGetUniformLocation(shader, "uMVP"),   1, GL_FALSE, glm::value_ptr(vp * model));
        glUniformMatrix4fv(glGetUniformLocation(shader, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        mesh->getMaterial().bindUniforms(shader);
        mesh->drawGeometry();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);


}
