module;
#include <memory>
#include "AntTweakBar.h"

export module postprocess.manager;

import ssao.postprocesseffect;
import ssr.postprocesseffect;
import Scene;
import Camera;
import tonemappingPPE;
import fog.postprocesseffect;
import fxaa.postprocesseffect;

// Owns all post-processing effects and their AntTweakBar UI.
export class PostProcessManager {
public:
    static constexpr int SLOT_SSAO    = 0;
    static constexpr int SLOT_SSR     = 1;
    static constexpr int SLOT_FOG     = 2;
    static constexpr int SLOT_TONEMAP = 3;
    static constexpr int SLOT_FXAA    = 4;

    std::shared_ptr<SSAOPostProcessEffect>        ssao;
    std::shared_ptr<SSRPostProcessEffect>         ssr;
    std::shared_ptr<ToneMappingPostProcessEffect> toneMap;
    std::shared_ptr<FogPostProcessEffect>         fog;
    std::shared_ptr<FXAAPostProcessEffect>        fxaa;

    float displayFPS     = 0.0f;
    float displayFrameMs = 0.0f;

    void init(Scene& scene, Camera& camera) {
        scene_ = &scene;

        ssao = std::make_shared<SSAOPostProcessEffect>(scene.getDeferred().getBufferManager());
        scene.addPostEffect(SLOT_SSAO, ssao);

        ssr = std::make_shared<SSRPostProcessEffect>(scene.getDeferred().getBufferManager());
        ssr->nearPlane = camera.getNearPlane();
        ssr->farPlane  = camera.getFarPlane();
        scene.addPostEffect(SLOT_SSR, ssr);

        toneMap = std::make_shared<ToneMappingPostProcessEffect>(scene.getDeferred().getBufferManager());
        toneMap->exposure = 0.3;
        toneMap->gamma    = 2.2f;

        scene.addPostEffect(SLOT_TONEMAP, toneMap);

        fog = std::make_shared<FogPostProcessEffect>(scene.getDeferred().getBufferManager());
        fog->nearPlane  = camera.getNearPlane();
        fog->farPlane   = camera.getFarPlane();
        fog->SetMatrices(camera.getView(), camera.getProjection());
        fog->SetCameraPos(camera.getPosition());
        scene.addPostEffect(SLOT_FOG, fog);

        fxaa = std::make_shared<FXAAPostProcessEffect>(scene.getDeferred().getBufferManager());
        scene.addPostEffect(SLOT_FXAA, fxaa);
    }


    void initUI() {
        TwInit(TW_OPENGL_CORE, nullptr);

        TwBar* bar = TwNewBar("Debug");
        TwDefine(" Debug label='Render Debug' size='260 500' color='30 30 30' alpha=210 position='10 10' ");

        TwAddSeparator(bar, "togglesSep", " label='Post Processing Toggles' ");
        TwAddVarCB(bar, "EnableSSAO",    TW_TYPE_BOOLCPP, SetSSAOCB,    GetSSAOCB,    this, " label='Ambient Occlusion (SSAO)' ");
        TwAddVarCB(bar, "EnableSSR",     TW_TYPE_BOOLCPP, SetSSRCB,     GetSSRCB,     this, " label='Screen Space Reflections' ");
        TwAddVarCB(bar, "EnableToneMap", TW_TYPE_BOOLCPP, SetToneMapCB, GetToneMapCB, this, " label='Tone Mapping' ");
        TwAddVarCB(bar, "EnableFog",     TW_TYPE_BOOLCPP, SetFogCB,     GetFogCB,     this, " label='Exponential Height Fog' ");
        TwAddVarCB(bar, "EnableFXAA",    TW_TYPE_BOOLCPP, SetFXAACB,    GetFXAACB,    this, " label='FXAA Anti-Aliasing' ");

        TwAddSeparator(bar, "ssaoSep", " label='SSAO Settings' ");
        TwAddVarRW(bar, "SSAORadius",   TW_TYPE_FLOAT, &ssao->radius,
                   " label='Radius' min=0.05 max=10.0 step=0.05 "
                   " help='Hemisphere sampling radius in view-space units.' ");
        TwAddVarRW(bar, "SSAOBias",     TW_TYPE_FLOAT, &ssao->bias,
                   " label='Bias' min=0.001 max=0.1 step=0.001 "
                   " help='Depth bias to prevent self-occlusion.' ");
        TwAddVarRW(bar, "SSAOStrength", TW_TYPE_FLOAT, &ssao->strength,
                   " label='Strength' min=0.5 max=5.0 step=0.1 "
                   " help='Power curve exponent applied to AO factor.' ");

        TwAddSeparator(bar, "shadowSep", " label='Shadow Settings' ");
        TwAddVarRW(bar, "ShadowBiasMin", TW_TYPE_FLOAT,
                   &scene_->getDeferred().shadowBiasMin,
                   " label='Bias Min' min=0.00001 max=0.01 step=0.00001 precision=5 "
                   " help='NDC depth bias on surfaces facing the light. Raise if you see shadow acne.' ");
        TwAddVarRW(bar, "ShadowBiasMax", TW_TYPE_FLOAT,
                   &scene_->getDeferred().shadowBiasMax,
                   " label='Bias Max' min=0.00001 max=0.01 step=0.00001 precision=5 "
                   " help='NDC depth bias at grazing angles. Raise if acne appears on steep surfaces.' ");

        TwAddSeparator(bar, "iblSep", " label='IBL Settings' ");
        TwAddVarRW(bar, "IBLDiffuseScale", TW_TYPE_FLOAT,
                   &scene_->getDeferred().iblDiffuseScale,
                   " label='Diffuse Scale' min=0.0 max=2.0 step=0.01 precision=2 "
                   " help='Scales the skybox IBL diffuse contribution. 0=use flat ambient, 1=full IBL brightness.' ");

        TwAddSeparator(bar, "tmSep", " label='Tone Mapping Settings' ");
        TwAddVarRW(bar, "TMExposure", TW_TYPE_FLOAT, &toneMap->exposure,
                   " label='Exposure' min=0.1 max=10.0 step=0.05 "
                   " help='Pre-tone-map exposure multiplier.' ");
        TwAddVarRW(bar, "TMGamma",    TW_TYPE_FLOAT, &toneMap->gamma,
                   " label='Gamma' min=1.0 max=3.0 step=0.05 "
                   " help='Display gamma (sRGB = 2.2).' ");

        TwAddSeparator(bar, "fxaaSep", " label='FXAA Settings' ");
        TwAddVarRW(bar, "FXAASubpixel",      TW_TYPE_FLOAT, &fxaa->subpixelAA,
                   " label='Subpixel AA' min=0.0 max=1.0 step=0.05 "
                   " help='Subpixel aliasing removal strength (0=off, 0.75=default).' ");
        TwAddVarRW(bar, "FXAAEdgeThreshold", TW_TYPE_FLOAT, &fxaa->edgeThreshold,
                   " label='Edge Threshold' min=0.063 max=0.333 step=0.01 "
                   " help='Minimum local contrast to trigger AA.' ");

        TwAddSeparator(bar, "fogSep", " label='Fog Settings' ");
        TwAddVarRW(bar, "FogDepthDensity",  TW_TYPE_FLOAT, &fog->fogDepthDensity,  " label='Depth Density' step=0.001 ");
        TwAddVarRW(bar, "FogHeightDensity", TW_TYPE_FLOAT, &fog->fogHeightDensity, " label='Height Density' step=0.05 ");
        TwAddVarRW(bar, "FogStartDepth",    TW_TYPE_FLOAT, &fog->fogStartDepth,    " label='Start Depth' step=1.0 ");
        TwAddVarRW(bar, "FogEndDepth",      TW_TYPE_FLOAT, &fog->fogEndDepth,      " label='End Depth' step=1.0 ");
        TwAddVarRW(bar, "FogStartHeight",   TW_TYPE_FLOAT, &fog->fogStartHeight,   " label='Start Height' step=0.5 ");
        TwAddVarRW(bar, "FogEndHeight",     TW_TYPE_FLOAT, &fog->fogEndHeight,     " label='End Height' step=0.5 ");
        TwAddVarRW(bar, "FogColor",         TW_TYPE_COLOR3F, &fog->fogColor,       " label='Fog Color' ");

        TwBar* perfBar = TwNewBar("Perf");
        TwDefine(" Perf label='Performance' size='180 70' color='20 20 20' alpha=200 "
                 " position='282 10' valueswidth=80 ");
        TwAddVarRO(perfBar, "FPS",     TW_TYPE_FLOAT, &displayFPS,     " label='FPS' precision=1 ");
        TwAddVarRO(perfBar, "FrameMs", TW_TYPE_FLOAT, &displayFrameMs, " label='Frame ms' precision=2 ");
    }

    void updatePerf(float dt) {
        if (dt > 0.0f) {
            const float instFPS = 1.0f / dt;
            const float instMs  = dt * 1000.0f;
            displayFPS     = displayFPS     + PERF_ALPHA * (instFPS - displayFPS);
            displayFrameMs = displayFrameMs + PERF_ALPHA * (instMs  - displayFrameMs);
        }
    }

    void updateMatrices(Camera& camera) {
        ssao->SetMatrices(camera.getView(), camera.getProjection());
        ssr->SetMatrices(camera.getView(), camera.getProjection());
        fog->nearPlane = camera.getNearPlane();
        fog->farPlane  = camera.getFarPlane();
        fog->SetMatrices(camera.getView(), camera.getProjection());
        fog->SetCameraPos(camera.getPosition());
    }

    void resize(int w, int h) {
        ssao->Resize(w, h);
        fxaa->Resize(w, h);
    }

private:
    static constexpr float PERF_ALPHA = 0.1f;

    Scene* scene_ = nullptr;

    bool enableSSAO    = true;
    bool enableSSR     = true;
    bool enableToneMap = true;
    bool enableFog     = true;
    bool enableFXAA    = true;

    static void TW_CALL SetSSAOCB(const void* value, void* clientData) {
        auto* self = static_cast<PostProcessManager*>(clientData);
        self->enableSSAO = *static_cast<const bool*>(value);
        if (self->enableSSAO) self->scene_->addPostEffect(PostProcessManager::SLOT_SSAO, self->ssao);
        else                  self->scene_->removePostEffect(PostProcessManager::SLOT_SSAO);
    }
    static void TW_CALL GetSSAOCB(void* value, void* clientData) {
        *static_cast<bool*>(value) = static_cast<PostProcessManager*>(clientData)->enableSSAO;
    }

    static void TW_CALL SetSSRCB(const void* value, void* clientData) {
        auto* self = static_cast<PostProcessManager*>(clientData);
        self->enableSSR = *static_cast<const bool*>(value);
        if (self->enableSSR) self->scene_->addPostEffect(PostProcessManager::SLOT_SSR, self->ssr);
        else                 self->scene_->removePostEffect(PostProcessManager::SLOT_SSR);
    }
    static void TW_CALL GetSSRCB(void* value, void* clientData) {
        *static_cast<bool*>(value) = static_cast<PostProcessManager*>(clientData)->enableSSR;
    }

    static void TW_CALL SetToneMapCB(const void* value, void* clientData) {
        auto* self = static_cast<PostProcessManager*>(clientData);
        self->enableToneMap = *static_cast<const bool*>(value);
        if (self->enableToneMap) self->scene_->addPostEffect(PostProcessManager::SLOT_TONEMAP, self->toneMap);
        else                     self->scene_->removePostEffect(PostProcessManager::SLOT_TONEMAP);
    }
    static void TW_CALL GetToneMapCB(void* value, void* clientData) {
        *static_cast<bool*>(value) = static_cast<PostProcessManager*>(clientData)->enableToneMap;
    }

    static void TW_CALL SetFogCB(const void* value, void* clientData) {
        auto* self = static_cast<PostProcessManager*>(clientData);
        self->enableFog = *static_cast<const bool*>(value);
        if (self->enableFog) self->scene_->addPostEffect(PostProcessManager::SLOT_FOG, self->fog);
        else                 self->scene_->removePostEffect(PostProcessManager::SLOT_FOG);
    }
    static void TW_CALL GetFogCB(void* value, void* clientData) {
        *static_cast<bool*>(value) = static_cast<PostProcessManager*>(clientData)->enableFog;
    }

    static void TW_CALL SetFXAACB(const void* value, void* clientData) {
        auto* self = static_cast<PostProcessManager*>(clientData);
        self->enableFXAA = *static_cast<const bool*>(value);
        if (self->enableFXAA) self->scene_->addPostEffect(PostProcessManager::SLOT_FXAA, self->fxaa);
        else                  self->scene_->removePostEffect(PostProcessManager::SLOT_FXAA);
    }
    static void TW_CALL GetFXAACB(void* value, void* clientData) {
        *static_cast<bool*>(value) = static_cast<PostProcessManager*>(clientData)->enableFXAA;
    }
};
