module;
#include <memory>
#include <filesystem>
#include "pgr.h"
module Scene;

import Camera;
import LightSource;
import IRenderer;
import forwardrenderer;
import Sphere;
import boundingBox;
import Material;

import Logger;
import RenderBufferManager;

bool Scene::loadSkybox(std::filesystem::path dir) {
    if (!deferredRenderer.loadSkybox(dir.string().c_str())) return false;
    forwardRenderer.setSkybox(deferredRenderer.getSkyboxCubemap(),
                              deferredRenderer.getSkyboxMaxLod());
    return true;
}

bool Scene::init(int width, int height) {
    if (!deferredRenderer.init(width, height))    return false;
    if (!forwardRenderer.init(width, height))     return false;
    if (!postProcessRenderer.init(width, height, deferredRenderer.getBufferManager())) return false;

    forwardRenderer.setTargetFBO(deferredRenderer.getBufferManager().getHandle(ENamedBuffer::SceneColor));
    return true;
}

void Scene::resize(int width, int height) {
    deferredRenderer.resize(width, height);
    if (!postProcessRenderer.resize(width, height))
        std::cerr << "Scene: postProcessRenderer resize failed\n";
}

StaticMesh* Scene::addMesh(std::filesystem::path modelPath, RenderMode mode) {
    auto sm = std::make_unique<StaticMesh>();
    sm->setRenderMode(mode);
    if (!sm->load(modelPath))
        return nullptr;

    StaticMesh* ptr = sm.get();
    staticMeshes.push_back(std::move(sm));
    return ptr;
}

Model* Scene::addSphere(const glm::vec3& position, RenderMode mode) {
    auto model = std::make_unique<Model>();
    model->setRenderMode(mode);
    model->uploadGeometry(SPHERE_VERTICES.data(),
                         static_cast<int>(SPHERE_VERTICES.size() * sizeof(Vertex)),
                         SPHERE_INDICES.data(),
                         static_cast<int>(SPHERE_INDICES.size()));

    auto material = std::make_unique<Material>();
    material->setAlbedo(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    material->setMetallic(1.0f);
    material->setRoughness(0.0f);

    Material* matPtr = material.get();
    model->setMaterial(matPtr);
    materialRegistry.push_back(std::move(material));

    BoundingBox bounds;
    bounds.min = glm::vec3(-1.0f);
    bounds.max = glm::vec3(1.0f);
    model->setLocalBounds(bounds);
    model->setModelMatrix(glm::translate(glm::mat4(1.0f), position));

    Model* ptr = model.get();
    rawModels.push_back(std::move(model));
    return ptr;
}

PointLight& Scene::addPointLight(const PointLight& light) {
    auto& sl  = sceneLights.emplace_back();
    sl.light  = std::make_unique<PointLight>(light);
    sl.visualMesh = nullptr;
    return static_cast<PointLight&>(*sl.light);
}

DirectionalLight& Scene::addDirectionalLight(const DirectionalLight& light) {
    auto& sl  = sceneLights.emplace_back();
    sl.light  = std::make_unique<DirectionalLight>(light);
    sl.visualMesh = nullptr;
    return static_cast<DirectionalLight&>(*sl.light);
}

SpotLight& Scene::addSpotLight(const SpotLight& light) {
    auto& sl  = sceneLights.emplace_back();
    sl.light  = std::make_unique<SpotLight>(light);
    sl.visualMesh = nullptr;
    return static_cast<SpotLight&>(*sl.light);
}


void Scene::draw(Camera& camera) {
    LightEnvironment env;
    env.ambient = ambientLight;

    for (auto& sl : sceneLights) {
        if (auto* pl = dynamic_cast<PointLight*>(sl.light.get()))
            env.pointLights.push_back(pl);
        else if (auto* dl = dynamic_cast<DirectionalLight*>(sl.light.get()))
            env.dirLights.push_back(dl);
        else if (auto* sp = dynamic_cast<SpotLight*>(sl.light.get()))
            env.spotLights.push_back(sp);
    } 

    std::vector<Model*> deferred, forward;

    for (auto& sm : staticMeshes)
        for (auto& m : sm->getAllModels())
            (m->getRenderMode() == RenderMode::Deferred ? deferred : forward).push_back(m.get());

    for (auto& m : rawModels)
        (m->getRenderMode() == RenderMode::Deferred ? deferred : forward).push_back(m.get());

    deferredRenderer.render(deferred, env, camera);
    forwardRenderer.render(forward,   env, camera);
 
    postProcessRenderer.render(deferredRenderer.getBufferManager().getHandle(ENamedBuffer::SceneColor));
}

void Scene::addPostEffect(unsigned int order, std::shared_ptr<IPostProcessEffect> effect) {
    postProcessRenderer.addEffect(order, std::move(effect));
}

void Scene::removePostEffect(unsigned int order) {
    postProcessRenderer.removeEffect(order);
}
