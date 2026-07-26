module;
#include <vector>
#include <memory>
#include <filesystem>
#include <glm/glm.hpp>
#include "pgr.h"

export module Scene;

import Model;
import StaticMesh;
export import LightSource;
export import lightsource.directional;
export import lightsource.point;
export import lightsource.spotlight;
import deferredrenderer;
import forwardrenderer;
import postprocess.renderer;

import Camera;
import PostProcessEffect;
import Sphere;

export class Scene {
    struct SceneLight {
        std::unique_ptr<LightSource> light;
        Model* visualMesh = nullptr;
    };

    std::vector<std::unique_ptr<StaticMesh>> staticMeshes;
    std::vector<std::unique_ptr<Model>>      rawModels;
    std::vector<std::unique_ptr<Material>>   materialRegistry;
    std::vector<SceneLight>                  sceneLights;
    glm::vec3                                ambientLight{0.1f};

    DeferredRenderer    deferredRenderer;
    ForwardRenderer     forwardRenderer;
    PostProcessRenderer postProcessRenderer;

public:
    Scene() = default;

    bool init(int width, int height);
    void resize(int width, int height);

    StaticMesh* addMesh(std::filesystem::path modelPath, RenderMode mode = RenderMode::Auto);

    Model* addSphere(const glm::vec3& position, RenderMode mode = RenderMode::Deferred);

    PointLight&       addPointLight      (const PointLight&       light);
    DirectionalLight& addDirectionalLight(const DirectionalLight& light);
    SpotLight&        addSpotLight       (const SpotLight&        light);

    void setAmbientLight(const glm::vec3& color) { ambientLight = color; }

    void draw(Camera& camera);

    void addPostEffect(unsigned int order, std::shared_ptr<IPostProcessEffect> effect);
    void removePostEffect(unsigned int order);

    bool loadSkybox(std::filesystem::path dir);

    DeferredRenderer& getDeferred() { return deferredRenderer; }

};
