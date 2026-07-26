
import tonemap.postprocesseffect;
import ssr.postprocesseffect;
import fog.postprocesseffect;

export module postprocesseffects;

export struct PostProcessEffects {
    ToneMappingEffect      toneMap;
    SSREffect              SSR;
    SSAOEffect             ssao;
    FogPostProcessEffect   fog;
};