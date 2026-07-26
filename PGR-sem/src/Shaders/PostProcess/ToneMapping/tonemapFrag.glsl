#version 330 core

in  vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uScene;
uniform float     uExposure; 
uniform float     uGamma;    

vec3 acesTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, vTexCoord).rgb;

    hdr *= uExposure;

    vec3 ldr = acesTonemap(hdr);
    ldr = pow(max(ldr, vec3(0.0)), vec3(1.0 / uGamma));

    fragColor = vec4(ldr, 1.0);
}
