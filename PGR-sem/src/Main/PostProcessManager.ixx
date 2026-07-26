module;
#include <memory>

export module postprocess.manager;

import ssao.postprocesseffect;
import ssr.postprocesseffect;
import Scene;
import Camera;
import tonemappingPPE;
import fog.postprocesseffect;
import fxaa.postprocesseffect;

// Owns all post-processing effects
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


    /// Enable/disable an effect by SLOT_* id, keeping the scene's effect chain in sync.
    void setEffectEnabled(int slot, bool enabled) {
        switch (slot) {
            case SLOT_SSAO:    enableSSAO    = enabled; apply(slot, enabled, ssao);    break;
            case SLOT_SSR:     enableSSR     = enabled; apply(slot, enabled, ssr);     break;
            case SLOT_FOG:     enableFog     = enabled; apply(slot, enabled, fog);     break;
            case SLOT_TONEMAP: enableToneMap = enabled; apply(slot, enabled, toneMap); break;
            case SLOT_FXAA:    enableFXAA    = enabled; apply(slot, enabled, fxaa);    break;
            default: break;
        }
    }

    [[nodiscard]] bool isEffectEnabled(int slot) const {
        switch (slot) {
            case SLOT_SSAO:    return enableSSAO;
            case SLOT_SSR:     return enableSSR;
            case SLOT_FOG:     return enableFog;
            case SLOT_TONEMAP: return enableToneMap;
            case SLOT_FXAA:    return enableFXAA;
            default:           return false;
        }
    }

    /// Pointer to the flag backing a slot, for binding straight to ImGui::Checkbox.
    [[nodiscard]] bool* effectFlag(int slot) {
        switch (slot) {
            case SLOT_SSAO:    return &enableSSAO;
            case SLOT_SSR:     return &enableSSR;
            case SLOT_FOG:     return &enableFog;
            case SLOT_TONEMAP: return &enableToneMap;
            case SLOT_FXAA:    return &enableFXAA;
            default:           return nullptr;
        }
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

    template <typename EFFECT>
    void apply(int slot, bool enabled, const EFFECT& effect) {
        if (enabled) scene_->addPostEffect(slot, effect);
        else         scene_->removePostEffect(slot);
    }
};
