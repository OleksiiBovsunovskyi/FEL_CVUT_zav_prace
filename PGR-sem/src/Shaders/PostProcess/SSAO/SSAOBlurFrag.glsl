#version 330 core

in  vec2  vTexCoord;
out float fragColor;

uniform sampler2D uSSAO;

// Simple 5×5 box blur to smooth the noisy SSAO result.
void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(uSSAO, 0));
    float result = 0.0;

    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(uSSAO, vTexCoord + offset).r;
        }
    }

    fragColor = result / 25.0;
}
