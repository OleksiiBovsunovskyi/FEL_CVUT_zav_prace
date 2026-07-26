#version 330 core

vec3 reconstructWorldPos(vec2 uv, float depth, mat4 invViewProj) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invViewProj * ndc;
    return worldPos.xyz / worldPos.w;
}
in  vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform mat4      uInvViewProj;

uniform float uFogDepthDensity; 
uniform float uFogHeightDensity;
uniform float uFogStartDepth;   
uniform float uFogEndDepth;     
uniform float uFogStartHeight;  
uniform float uFogEndHeight;    
uniform vec3 uFogColor;
uniform vec3 uCameraPos;





void main() {
    vec3 worldPos = reconstructWorldPos(vTexCoord, texture(uDepth, vTexCoord).r, uInvViewProj);
    
    float height = clamp(worldPos.y, uFogStartHeight, uFogEndHeight);
    float heightLinearFactor = 1 - (height - uFogStartHeight) / (uFogEndHeight - uFogStartHeight);
    float heightExpFactor = 1 - exp(-heightLinearFactor * uFogHeightDensity);

    float dist = distance(worldPos, uCameraPos);
    float clampedDist = clamp(dist, uFogStartDepth, uFogEndDepth);
    float distLinear = (clampedDist - uFogStartDepth) / (uFogEndDepth - uFogStartDepth);
    float depthFactor = 1.0 - exp(-uFogDepthDensity * distLinear);
    
    float fogFactor = heightExpFactor * depthFactor;
    
    fragColor = mix(texture(uScene, vTexCoord), vec4(uFogColor, 1.0), fogFactor);
    
    return;
}

