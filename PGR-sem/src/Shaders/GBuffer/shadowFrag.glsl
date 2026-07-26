#version 460 core

in vec2 vTexCoord;

uniform bool      uUseTexture;
uniform sampler2D uAlbedoTexture;
uniform float     uAlphaThreshold;

void main() {
    if (uUseTexture && uAlphaThreshold > 0.0)
        if (texture(uAlbedoTexture, vTexCoord).a < uAlphaThreshold) discard;
}
