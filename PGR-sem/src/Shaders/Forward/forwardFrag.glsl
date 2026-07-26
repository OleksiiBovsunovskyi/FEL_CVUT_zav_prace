#version 330 core

in vec3 vFragPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vTexCoord;

uniform sampler2D uAlbedoTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uORMTexture;
uniform sampler2D uEmissiveTexture;

layout(std140) uniform MaterialBlock {
    vec4  uAlbedo;            
    vec3  uEmissiveColor;     
    float uEmissiveIntensity; 
    float uSpecularIntensity; 
    float uShininess;         
    float uMetallic;          
    float uRoughness;         
    float uAlphaThreshold;    
    int   uEmissive;          
    int   uUseTexture;        
    int   uUseNormalMap;      
    int   uUseORMMap;         
    int   _pad2;              
    int   uUseEmissiveTexture;
};

uniform samplerCube uSkybox;

#define MAX_POINT_LIGHTS 8
#define MAX_DIR_LIGHTS   4
#define MAX_SPOT_LIGHTS  8

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
    vec3 uCameraPos; 
    float uSkyboxMaxLod;
    vec3 uAmbient;
    float uShadowBiasMin;
    float uShadowBiasMax;
    int uHasSkybox;
    int uNumShadowLights;
    int uNumPointLights;
    int uNumDirLights;
    int uNumSpotLights;
    float uIBLDiffuseScale;
    int _pad1;
    PointLightData uPointLights[MAX_POINT_LIGHTS];
    DirLightData   uDirLights[MAX_DIR_LIGHTS];
    SpotLightData  uSpotLights[MAX_SPOT_LIGHTS];
    ShadowSlot     uShadowSlots[4];
};

out vec4 fragColor;

vec3 computePointLight(PointLightData light, vec3 fragPos, vec3 normal,
                       vec3 albedo, float specInt, float shininess, float metallic) {
    vec3  dir = normalize(light.position - fragPos);
    float diff = max(dot(normal, dir), 0.0);
    vec3  viewDir = normalize(uCameraPos - fragPos); 
    vec3  halfDir = normalize(dir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    vec3  specColor = mix(vec3(1.0), albedo, metallic);
    float kD = (1.0 - specInt) * (1.0 - metallic);
    vec3  diffuse = kD * diff * light.color * albedo;
    vec3  specular = specInt * spec * light.color * specColor;
    return (diffuse + specular) * attenuation;
}

vec3 computeDirLight(DirLightData light, vec3 fragPos, vec3 normal,
                     vec3 albedo, float specInt, float shininess, float metallic) {
    vec3  dir = normalize(-light.direction);
    float diff = max(dot(normal, dir), 0.0);
    vec3  viewDir = normalize(uCameraPos - fragPos);
    vec3  halfDir = normalize(dir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
    vec3  specColor = mix(vec3(1.0), albedo, metallic);
    float kD = (1.0 - specInt) * (1.0 - metallic);
    vec3  diffuse = kD * diff * light.color * albedo;
    vec3  specular = specInt * spec * light.color * specColor;
    return diffuse + specular;
}

vec3 computeSpotLight(SpotLightData light, vec3 fragPos, vec3 normal,
                      vec3 albedo, float specInt, float shininess, float metallic) {
    vec3  dir = normalize(light.position - fragPos);
    float diff = max(dot(normal, dir), 0.0);
    vec3  viewDir = normalize(uCameraPos - fragPos);
    vec3  halfDir = normalize(dir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
    float dist = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    float theta = dot(dir, normalize(-light.direction));
    float epsilon = light.innerCutoff - light.outerCutoff;
    float intensity = clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);
    vec3  specColor = mix(vec3(1.0), albedo, metallic);
    float kD = (1.0 - specInt) * (1.0 - metallic);
    vec3  diffuse = kD * diff * light.color * albedo;
    vec3  specular = specInt * spec * light.color * specColor;
    return (diffuse + specular) * attenuation * intensity;
}

void main() {
    vec4  texSample = uUseTexture != 0 ? texture(uAlbedoTexture, vTexCoord) : uAlbedo;
    float alpha = texSample.a;

    if (uUseTexture != 0 && uAlphaThreshold > 0.0)
        if (alpha < uAlphaThreshold) discard;

    vec3 albedo = texSample.rgb;

    if (uEmissive != 0) {
        fragColor = vec4(albedo, alpha);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent.xyz - dot(vTangent.xyz, N) * N);
    vec3 B = cross(N, T) * vTangent.w; // w = bitangent sign from vertex data
    mat3 TBN = mat3(T, B, N);

    vec3 normal;
    if (uUseNormalMap != 0) {
        vec3 tn = texture(uNormalTexture, vTexCoord).rgb * 2.0 - 1.0;
        normal = normalize(TBN * tn);
    } else {
        normal = N;
    }

    vec3  ormSample = uUseORMMap != 0 ? texture(uORMTexture, vTexCoord).rgb : vec3(1.0, uRoughness, uMetallic);
    float aoVal = ormSample.r;
    float roughnessVal = ormSample.g;
    float metallicVal = ormSample.b;
    float derivedShininess = max((1.0 - roughnessVal) * (1.0 - roughnessVal) * 256.0, 1.0);
    float smoothness = 1.0 - roughnessVal;
    float scaledSpecInt = uSpecularIntensity * smoothness * smoothness;

    vec3 result;
    if (uHasSkybox != 0) {
        vec3  F0IBL = mix(vec3(0.04), albedo, metallicVal);
        float NdotVIBL = max(dot(normal, normalize(uCameraPos - vFragPos)), 0.0);
        vec3  FIBL = F0IBL + (1.0 - F0IBL) * pow(1.0 - NdotVIBL, 5.0);
        vec3  envDiff = textureLod(uSkybox, normal, uSkyboxMaxLod).rgb;
        result = envDiff * uIBLDiffuseScale * albedo * (vec3(1.0) - FIBL) * (1.0 - metallicVal) * aoVal;
    } else {
        result = uAmbient * albedo * aoVal;
    }

    for (int i = 0; i < uNumPointLights; i++)
        result += computePointLight(uPointLights[i], vFragPos, normal,
                                    albedo, scaledSpecInt, derivedShininess, metallicVal);

    for (int i = 0; i < uNumDirLights; i++)
        result += computeDirLight(uDirLights[i], vFragPos, normal,
                                  albedo, scaledSpecInt, derivedShininess, metallicVal);

    for (int i = 0; i < uNumSpotLights; i++)
        result += computeSpotLight(uSpotLights[i], vFragPos, normal,
                                   albedo, scaledSpecInt, derivedShininess, metallicVal);

    if (uHasSkybox != 0) {
        vec3  viewDir = normalize(uCameraPos - vFragPos);
        vec3  F0 = mix(vec3(0.04), albedo, metallicVal);
        float NdotV = max(dot(normal, viewDir), 0.0);
        vec3  F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
        vec3  reflDir = reflect(-viewDir, normal);
        vec3  envSpec = textureLod(uSkybox, reflDir, roughnessVal * uSkyboxMaxLod).rgb;
        result += envSpec * F;
    }

    fragColor = vec4(result, alpha);
}

