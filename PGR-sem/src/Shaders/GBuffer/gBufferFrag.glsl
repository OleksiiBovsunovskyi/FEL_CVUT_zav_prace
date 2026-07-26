#version 330 core

in vec3 vFragPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vTexCoord;
in vec4 vCurrentClipPos;
in vec4 vPrevClipPos;

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

layout(location = 0) out vec3 gPosition;
layout(location = 1) out vec4 gNormal;      
layout(location = 2) out vec4 gAlbedoSpec; 
layout(location = 3) out vec2 gMotionVec;  
layout(location = 4) out vec2 gMetallicAO; 
layout(location = 5) out vec3 gEmissive;    

void main() {
    // Build TBN matrix for normal mapping
    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent.xyz - dot(vTangent.xyz, N) * N); 
    vec3 B = cross(N, T) * vTangent.w;  
    mat3 TBN = mat3(T, B, N);

    vec3 normal;
    if (uUseNormalMap != 0) {
        vec3 tangentNormal = texture(uNormalTexture, vTexCoord).rgb * 2.0 - 1.0;
        normal = normalize(TBN * tangentNormal);
    } else {
        normal = N;
    }

    // Alpha cutout
    if (uUseTexture != 0 && uAlphaThreshold > 0.0) {
        if (texture(uAlbedoTexture, vTexCoord).a < uAlphaThreshold) discard;
    }

    vec3  ormSample = uUseORMMap != 0 ? texture(uORMTexture, vTexCoord).rgb : vec3(1.0, uRoughness, uMetallic);
    float aoVal = ormSample.r;
    float roughnessVal = ormSample.g;
    float metallicVal = ormSample.b;

  
    float derivedShininess = max((1.0 - roughnessVal) * (1.0 - roughnessVal) * 256.0, 1.0);
    
    gPosition = vFragPos;
    gNormal = vec4(normal, derivedShininess / 256.0);
    gAlbedoSpec.rgb = uUseTexture != 0 ? texture(uAlbedoTexture, vTexCoord).rgb : uAlbedo.rgb;
    float smoothness = 1.0 - roughnessVal;
    gAlbedoSpec.a = uSpecularIntensity * smoothness * smoothness;
    gMetallicAO = vec2(metallicVal, aoVal);

    vec3 emissiveTex = uUseEmissiveTexture != 0 ? texture(uEmissiveTexture, vTexCoord).rgb : vec3(1.0);
    gEmissive = uEmissiveColor * emissiveTex * uEmissiveIntensity;

    vec2 currentNDC = vCurrentClipPos.xy / vCurrentClipPos.w;
    vec2 prevNDC = vPrevClipPos.xy    / vPrevClipPos.w;
    gMotionVec = (currentNDC - prevNDC) * 0.5;
}

