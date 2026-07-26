#version 330 core

in  vec2  vTexCoord;
out float fragColor;

// ── G-buffer samplers ─────────────────────────────────────────────────────────
uniform sampler2D uPosition;   // world-space position (RGB32F)
uniform sampler2D uNormal;     // world-space normal   (RGBA16F, xyz used)
uniform sampler2D uNoise;      // 4×4 random rotation vectors (RGB16F)

// ── Camera ────────────────────────────────────────────────────────────────────
uniform mat4 uView;
uniform mat4 uProjection;

// ── Kernel ────────────────────────────────────────────────────────────────────
uniform vec3  uSamples[64];
uniform float uRadius;
uniform float uBias;
uniform vec2  uNoiseScale;   // screenWidth/4, screenHeight/4

// ── Main ──────────────────────────────────────────────────────────────────────
void main() {
    // ── Sample G-buffer ───────────────────────────────────────────────────────
    vec3 normalWS = texture(uNormal, vTexCoord).xyz;

    // Early-out: sky / empty pixels have no normal → no occlusion.
    if (dot(normalWS, normalWS) < 0.01) {
        fragColor = 1.0;
        return;
    }

    vec3 fragPosWS = texture(uPosition, vTexCoord).rgb;

    // ── Transform to view space ───────────────────────────────────────────────
    vec3 fragPosVS = (uView * vec4(fragPosWS, 1.0)).xyz;
    vec3 normalVS = normalize(mat3(uView) * normalize(normalWS));

    // ── Build TBN matrix ──────────────────────────────────────────────────────
    // Gram-Schmidt orthogonalisation against the per-pixel random rotation.
    vec3 randomVec = normalize(texture(uNoise, vTexCoord * uNoiseScale).rgb);
    vec3 tangent = normalize(randomVec - normalVS * dot(randomVec, normalVS));
    vec3 bitangent = cross(normalVS, tangent);
    mat3 TBN = mat3(tangent, bitangent, normalVS);

    // ── Hemisphere sampling ───────────────────────────────────────────────────
    float occlusion = 0.0;

    for (int i = 0; i < 32; ++i) {
        // Rotate sample into view space and offset from fragment position.
        vec3 sampleVS = fragPosVS + TBN * uSamples[i] * uRadius;

        // Project sample to get its screen UV.
        vec4 offset = uProjection * vec4(sampleVS, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        // Reject samples outside the screen.
        if (offset.x < 0.0 || offset.x > 1.0 ||
            offset.y < 0.0 || offset.y > 1.0)
            continue;

        // Depth of stored geometry at the projected UV (in view space).
        vec3 storedPosWS = texture(uPosition, offset.xy).rgb;
        float sampleDepth = (uView * vec4(storedPosWS, 1.0)).z;

        // Range check: ignore geometry far outside the sampling radius to avoid
        // halos around objects.
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / abs(fragPosVS.z - sampleDepth));

        // Occluded if the stored geometry is closer to the camera than the sample.
        // (Both z values are negative in view space; closer → less negative.)
        occlusion += (sampleDepth >= sampleVS.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }

    fragColor = 1.0 - (occlusion / 64.0);
}

