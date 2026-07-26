module;
#include <vector>
#include <glm/glm.hpp>
#ifdef _WIN32
#include "GL/GL.h"
#else
#include <GL/gl.h>
#endif
#include <cstddef>
export module IRenderer;

import Model;
import Camera;

import LightSource;
import lightsource.directional;
import lightsource.point;
import lightsource.spotlight;


export struct PointLightGPU
{
    glm::vec3 position;
    float _pad0;
    glm::vec3 color;
    float constant;
    float linear;
    float quadratic;
    float _pad1[2];
};

static_assert(sizeof(PointLightGPU) == 48);

export struct DirLightGPU
{
    glm::vec3 direction;
    float _pad0;
    glm::vec3 color;
    float _pad1;
};

static_assert(sizeof(DirLightGPU) == 32);

export struct SpotLightGPU
{
    glm::vec3 position;
    float _pad0;
    glm::vec3 direction;
    float _pad1;
    glm::vec3 color;
    float constant;
    float linear;
    float quadratic;
    float innerCutoff;
    float outerCutoff;
};

static_assert(sizeof(SpotLightGPU) == 64);

export struct ShadowSlotGPU
{
    glm::mat4 lightSpaceMatrix;
    int lightType;
    int lightIndex;
    float _pad[2];
};

static_assert(sizeof(ShadowSlotGPU) == 80);

export struct SceneLightUBO
{
    glm::vec3 cameraPos;
    float skyboxMaxLod;
    glm::vec3 ambient;
    float shadowBiasMin;
    float shadowBiasMax;
    int hasSkybox;
    int numShadowLights;
    int numPointLights;
    int numDirLights;
    int numSpotLights;
    float iblDiffuseScale;
    int _pad;

    PointLightGPU pointLights[8];
    DirLightGPU dirLights[4];
    SpotLightGPU spotLights[8];
    ShadowSlotGPU shadowSlots[4];
};

static_assert(sizeof(SceneLightUBO) == 1408);
static_assert(offsetof(SceneLightUBO, pointLights) == 64);
static_assert(offsetof(SceneLightUBO, dirLights) == 448);
static_assert(offsetof(SceneLightUBO, spotLights) == 576);
static_assert(offsetof(SceneLightUBO, shadowSlots) == 1088);

/// All lighting data passed to a renderer each frame.
export struct LightEnvironment
{
    std::vector<PointLight*> pointLights;
    std::vector<DirectionalLight*> dirLights;
    std::vector<SpotLight*> spotLights;
    glm::vec3 ambient{0.1f};
};

export class IRenderer
{
public:
    static constexpr int MAX_POINT_LIGHTS = 8;
    static constexpr int MAX_DIR_LIGHTS = 4;
    static constexpr int MAX_SPOT_LIGHTS = 8;

    virtual ~IRenderer() = default;

    virtual bool init(int width, int height) = 0;

    /**
     * Renders provided geometry, lights, transformed with camera
     * @param meshes -> geometry to render
     * @param lights -> lights to render
     * @param camera -> camera used
     ***/
    virtual void render(const std::vector<Model*>& meshes,
                        const LightEnvironment& lights,
                        const Camera& camera) = 0;


    virtual void resize(int width, int height) = 0;
};
