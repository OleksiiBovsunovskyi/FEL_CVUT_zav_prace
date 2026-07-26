#version 330 core

in  vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uColor;    
uniform sampler2D uDepth;    
uniform sampler2D uNormal;   
uniform sampler2D uPosition; 
uniform sampler2D uAlbedoSpec;

uniform mat4  uView;
uniform mat4  uProjection;
uniform float uNearPlane;
uniform float uFarPlane;

const int   MAX_STEPS = 64;
const int   REFINE_STEPS = 8;
const float STEP_SIZE = 0.2;  
const float MAX_DIST = 60.0; 


float linearizeDepth(float d) {
    float z = d * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane)
         / (uFarPlane + uNearPlane - z * (uFarPlane - uNearPlane));
}

vec2 viewToUV(vec3 vsPos) {
    vec4 clip = uProjection * vec4(vsPos, 1.0);
    return (clip.xy / clip.w) * 0.5 + 0.5;
}

bool inScreen(vec2 uv) {
    return all(greaterThanEqual(uv, vec2(0.0))) &&
           all(lessThanEqual   (uv, vec2(1.0)));
}

void main() {
    vec3 sceneCol = texture(uColor, vTexCoord).rgb;

    vec4 normData = texture(uNormal, vTexCoord);
    if (length(normData.xyz) < 0.01) {
        fragColor = vec4(sceneCol, 1.0);
        return;
    }

    float specular = texture(uAlbedoSpec, vTexCoord).a;
    if (specular < 0.05) {
        fragColor = vec4(sceneCol, 1.0);
        return;
    }

    vec3 fragPosWS = texture(uPosition, vTexCoord).rgb;
    vec3 normalWS = normalize(normData.xyz);

    vec3 fragPosVS = (uView * vec4(fragPosWS, 1.0)).xyz;
    vec3 normalVS = normalize(mat3(uView) * normalWS);
    vec3 viewDirVS = normalize(fragPosVS);  

    vec3 reflDirVS = reflect(viewDirVS, normalVS);


    if (reflDirVS.z >= 0.0) {
        fragColor = vec4(sceneCol, 1.0);
        return;
    }

    float NdotV = max(dot(-viewDirVS, normalVS), 0.0);
    float fresnel = mix(0.04, 1.0, pow(1.0 - NdotV, 5.0));

    float stepSize = STEP_SIZE;
    float prevT = 0.0;
    float t = stepSize;
    vec2  hitUV = vec2(0.0);
    bool  hasHit = false;

    for (int i = 0; i < MAX_STEPS; i++) {
        if (t > MAX_DIST) break;

        vec3 rayPosVS = fragPosVS + reflDirVS * t;
        vec2 uv = viewToUV(rayPosVS);

        if (!inScreen(uv)) break;

        float storedDepth = linearizeDepth(texture(uDepth, uv).r);
        float rayDepth = -rayPosVS.z;   

        float thickness = max(0.5, rayDepth * 0.04);

        float diff = rayDepth - storedDepth;
        if (diff > 0.0 && diff < thickness) {
            float lo = prevT, hi = t;
            for (int r = 0; r < REFINE_STEPS; r++) {
                float mid = (lo + hi) * 0.5;
                vec3  mPos = fragPosVS + reflDirVS * mid;
                vec2  mUV = viewToUV(mPos);
                float mDepth = -mPos.z;
                float mStored = linearizeDepth(texture(uDepth, mUV).r);
                if (mDepth > mStored) hi = mid;
                else                  lo = mid;
            }
            hitUV = viewToUV(fragPosVS + reflDirVS * ((lo + hi) * 0.5));
            hasHit = true;
            break;
        }

        prevT = t;
        stepSize *= 1.04;   
        t        += stepSize;
    }

    if (!hasHit) {
        fragColor = vec4(sceneCol, 1.0);
        return;
    }
    vec2  border = min(hitUV, 1.0 - hitUV);
    float edgeFade = smoothstep(0.0, 0.05, border.x) *
                     smoothstep(0.0, 0.05, border.y);

    float distFade = 1.0 - clamp(t / MAX_DIST, 0.0, 1.0);

    float weight = specular * fresnel * edgeFade * distFade;
    vec3  reflCol = texture(uColor, hitUV).rgb;

    fragColor = vec4(mix(sceneCol, reflCol, weight), 1.0);
}

