#version 330 core

in  vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uScene;
uniform vec2      uTexelSize;    // 1.0 / vec2(width, height)
uniform float     uSubpixelAA;  // 0.75 — subpixel aliasing removal strength
uniform float     uEdgeThreshold;     // 0.166 — minimum local contrast to apply AA
uniform float     uEdgeThresholdMin;  // 0.0833 — darkness threshold, avoids AA on very dark areas

// FXAA 3.11 quality (simplified port of Timothy Lottes' algorithm)

float rgb2luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 colorCenter = texture(uScene, vTexCoord).rgb;

    float lumaCenter = rgb2luma(colorCenter);
    float lumaDown = rgb2luma(texture(uScene, vTexCoord + vec2( 0.0, -uTexelSize.y)).rgb);
    float lumaUp = rgb2luma(texture(uScene, vTexCoord + vec2( 0.0,  uTexelSize.y)).rgb);
    float lumaLeft = rgb2luma(texture(uScene, vTexCoord + vec2(-uTexelSize.x,  0.0)).rgb);
    float lumaRight = rgb2luma(texture(uScene, vTexCoord + vec2( uTexelSize.x,  0.0)).rgb);

    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    // Skip pixels with insufficient contrast
    if (lumaRange < max(uEdgeThresholdMin, lumaMax * uEdgeThreshold)) {
        fragColor = vec4(colorCenter, 1.0);
        return;
    }

    float lumaDownLeft = rgb2luma(texture(uScene, vTexCoord + vec2(-uTexelSize.x, -uTexelSize.y)).rgb);
    float lumaUpRight = rgb2luma(texture(uScene, vTexCoord + vec2( uTexelSize.x,  uTexelSize.y)).rgb);
    float lumaUpLeft = rgb2luma(texture(uScene, vTexCoord + vec2(-uTexelSize.x,  uTexelSize.y)).rgb);
    float lumaDownRight = rgb2luma(texture(uScene, vTexCoord + vec2( uTexelSize.x, -uTexelSize.y)).rgb);

    float lumaDownUp = lumaDown    + lumaUp;
    float lumaLeftRight = lumaLeft    + lumaRight;
    float lumaLeftCorners = lumaDownLeft  + lumaUpLeft;
    float lumaDownCorners = lumaDownLeft  + lumaDownRight;
    float lumaRightCorners = lumaDownRight + lumaUpRight;
    float lumaUpCorners = lumaUpRight   + lumaUpLeft;

    // Estimate gradient along each axis
    float edgeHorizontal = abs(-2.0 * lumaLeft   + lumaLeftCorners)
                         + abs(-2.0 * lumaCenter  + lumaDownUp) * 2.0
                         + abs(-2.0 * lumaRight   + lumaRightCorners);
    float edgeVertical = abs(-2.0 * lumaUp      + lumaUpCorners)
                         + abs(-2.0 * lumaCenter  + lumaLeftRight) * 2.0
                         + abs(-2.0 * lumaDown    + lumaDownCorners);

    bool isHorizontal = edgeHorizontal >= edgeVertical;

    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp   : lumaRight;
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;
    bool is1Steepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    float stepLength = isHorizontal ? uTexelSize.y : uTexelSize.x;
    float lumaLocalAverage;
    if (is1Steepest) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    vec2 currentUV = vTexCoord;
    if (isHorizontal)
        currentUV.y += stepLength * 0.5;
    else
        currentUV.x += stepLength * 0.5;

    // Iterative search along the edge
    const int QUALITY_STEPS = 12;
    const float quality[12] = float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

    vec2 offset = isHorizontal ? vec2(uTexelSize.x, 0.0) : vec2(0.0, uTexelSize.y);
    vec2 uv1 = currentUV - offset;
    vec2 uv2 = currentUV + offset;

    float lumaEnd1 = rgb2luma(texture(uScene, uv1).rgb) - lumaLocalAverage;
    float lumaEnd2 = rgb2luma(texture(uScene, uv2).rgb) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;

    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    if (!reached1 || !reached2) {
        for (int i = 2; i < QUALITY_STEPS; i++) {
            if (!reached1) {
                lumaEnd1 = rgb2luma(texture(uScene, uv1).rgb) - lumaLocalAverage;
                reached1 = abs(lumaEnd1) >= gradientScaled;
            }
            if (!reached2) {
                lumaEnd2 = rgb2luma(texture(uScene, uv2).rgb) - lumaLocalAverage;
                reached2 = abs(lumaEnd2) >= gradientScaled;
            }
            if (reached1 && reached2) break;
            if (!reached1) uv1 -= offset * quality[i];
            if (!reached2) uv2 += offset * quality[i];
        }
    }

    float dist1 = isHorizontal ? (vTexCoord.x - uv1.x) : (vTexCoord.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - vTexCoord.x) : (uv2.y - vTexCoord.y);
    bool isDir1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / edgeLength + 0.5;

    bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
    bool correctVariation = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Subpixel AA
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
    float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
    float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
    float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * uSubpixelAA;
    finalOffset = max(finalOffset, subPixelOffsetFinal);

    vec2 finalUV = vTexCoord;
    if (isHorizontal)
        finalUV.y += finalOffset * stepLength;
    else
        finalUV.x += finalOffset * stepLength;

    fragColor = vec4(texture(uScene, finalUV).rgb, 1.0);
}

