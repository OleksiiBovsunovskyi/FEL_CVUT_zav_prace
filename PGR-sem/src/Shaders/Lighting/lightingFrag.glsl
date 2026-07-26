#version 460 core

in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D   gPosition;
uniform sampler2D   gNormal;
uniform sampler2D   gAlbedoSpec;
uniform sampler2D   uDepthMap;
uniform sampler2D   gMetallicAO;
uniform sampler2D   gEmissive;
uniform samplerCube uSkybox;
uniform sampler2DShadow uShadowMaps[4];

#define MAX_POINT_LIGHTS 8
#define MAX_DIR_LIGHTS   4
#define MAX_SPOT_LIGHTS  8
#define MAX_SHADOW_LIGHTS 4

struct PointLightData {
    vec3  position; float _pad0;
    vec3  color;    float constant;
    float linear; float quadratic; float _pad1; float _pad2;
};

struct DirLightData {
    vec3 direction; float _pad0;
    vec3 color;     float _pad1;
};

struct SpotLightData {
    vec3  position;  float _pad0;
    vec3  direction; float _pad1;
    vec3  color;     float constant;
    float linear; float quadratic; float innerCutoff; float outerCutoff;
};

struct ShadowSlot {
    mat4 lightSpaceMatrix;
    int  lightType;
    int  lightIndex;
    int  _pad0;
    int  _pad1;
};

layout(std140) uniform SceneLightBlock {
    vec3  uCameraPos;       float uSkyboxMaxLod;
    vec3  uAmbient;         float uShadowBiasMin;
    float uShadowBiasMax;
    int   uHasSkybox;
    int   uNumShadowLights;
    int   uNumPointLights;
    int   uNumDirLights;
    int   uNumSpotLights;
    float uIBLDiffuseScale;
    int   _pad1;
    PointLightData uPointLights[MAX_POINT_LIGHTS];
    DirLightData   uDirLights[MAX_DIR_LIGHTS];
    SpotLightData  uSpotLights[MAX_SPOT_LIGHTS];
    ShadowSlot     uShadowSlots[MAX_SHADOW_LIGHTS];
};

#include "Shaders/lib/blinn_phong.glsl"


float attenuation(float constant, float linear, float quadratic, float dist) {
    return 1.0 / (constant + linear * dist + quadratic * dist * dist);
}

float computeShadow(vec3 fragPos, vec3 normal, vec3 lightDir, int slot) {
    vec4 lsp = uShadowSlots[slot].lightSpaceMatrix * vec4(fragPos, 1.0);
    vec3 proj = lsp.xyz / lsp.w * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.z < 0.0 ||
        proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = mix(uShadowBiasMin, uShadowBiasMax, 1.0 - cosTheta);
    proj.z -= bias;

    // 3x3 PCF
    float shadowSum = 0.0;
    vec2  texel = 1.0 / vec2(textureSize(uShadowMaps[slot], 0));
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            shadowSum += texture(uShadowMaps[slot], proj + vec3(vec2(x, y) * texel, 0.0));
    return shadowSum / 9.0; 
}

float shadowForLight(vec3 fragPos, vec3 normal, vec3 lightDir, int lightType, int lightIndex) {

    for (int s = 0; s < uNumShadowLights; s++) {
        if (uShadowSlots[s].lightType == lightType &&
            uShadowSlots[s].lightIndex == lightIndex)
            return computeShadow(fragPos, normal, lightDir, s);
    }
    return 1.0;
}


vec3 computePointLight(PointLightData light, int idx, vec3 fragPos, vec3 normal,
                       vec3 viewDir, vec3 albedo, float specInt,
                       float shininess, float metallic) {
    vec3  dir = normalize(light.position - fragPos);
    float dist = length(light.position - fragPos);
    float att = attenuation(light.constant, light.linear, light.quadratic, dist);
    float shad = shadowForLight(fragPos, normal, dir, 0, idx);
    return blinnPhong(dir, light.color, normal, viewDir,
                      albedo, specInt, shininess, metallic) * att * shad;
}

vec3 computeDirLight(DirLightData light, int idx, vec3 fragPos, vec3 normal,
                     vec3 viewDir, vec3 albedo, float specInt,
                     float shininess, float metallic) {
    vec3 dir = normalize(-light.direction);
    float shad = shadowForLight(fragPos, normal, dir, 1, idx);
    return blinnPhong(dir, light.color, normal, viewDir,
                      albedo, specInt, shininess, metallic) * shad;
}

vec3 computeSpotLight(SpotLightData light, int idx, vec3 fragPos, vec3 normal,
                      vec3 viewDir, vec3 albedo, float specInt,
                      float shininess, float metallic) {
    vec3  dir = normalize(light.position - fragPos);
    float dist = length(light.position - fragPos);
    float att = attenuation(light.constant, light.linear, light.quadratic, dist);
    float theta = dot(dir, normalize(-light.direction));
    float epsilon = light.innerCutoff - light.outerCutoff;
    float intensity = clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);
    float shad = shadowForLight(fragPos, normal, dir, 2, idx);
    return blinnPhong(dir, light.color, normal, viewDir,
                      albedo, specInt, shininess, metallic) * att * intensity * shad;
}


void main() {

    vec4 normData = texture(gNormal, vTexCoord);
    if (length(normData.rgb) < 0.01) {
        fragColor = vec4(0.2, 0.1, 0.3, 1.0);
        return;
    }

    vec3  fragPos = texture(gPosition,   vTexCoord).rgb;
    vec3  normal = normalize(normData.rgb);
    float shininess = normData.a * 256.0;
    vec4  albSpec = texture(gAlbedoSpec, vTexCoord);
    vec3  albedo = albSpec.rgb;
    float specInt = albSpec.a;
    vec2  matAO = texture(gMetallicAO, vTexCoord).rg;
    float metallic = matAO.r;
    float ao = matAO.g;

    vec3  viewDir = normalize(uCameraPos - fragPos);
    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(normal, viewDir), 0.0);
    vec3  F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float roughness = 1.0 - sqrt(clamp(shininess / 256.0, 0.0, 1.0));

    vec3 result;
    if (uHasSkybox != 0) {
        vec3 envDiff = textureLod(uSkybox, normal, uSkyboxMaxLod).rgb;
        result = envDiff * uIBLDiffuseScale * albedo * (vec3(1.0) - F) * (1.0 - metallic) * ao;
    } else {
        result = uAmbient * albedo * ao;
    }

    for (int i = 0; i < uNumPointLights; i++)
        result += computePointLight(uPointLights[i], i, fragPos, normal,
                                    viewDir, albedo, specInt, shininess, metallic);

    for (int i = 0; i < uNumDirLights; i++)
        result += computeDirLight(uDirLights[i], i, fragPos, normal,
                                  viewDir, albedo, specInt, shininess, metallic);

    for (int i = 0; i < uNumSpotLights; i++)
        result += computeSpotLight(uSpotLights[i], i, fragPos, normal,
                                   viewDir, albedo, specInt, shininess, metallic);

    // Specular IBL
    if (uHasSkybox != 0) {
        vec3 reflDir = reflect(-viewDir, normal);
        vec3 envSpec = textureLod(uSkybox, reflDir, roughness * uSkyboxMaxLod).rgb;
        result += envSpec * F * ao * uIBLDiffuseScale;
    }

    result += texture(gEmissive, vTexCoord).rgb;

    fragColor = vec4(result, 1.0);
}

