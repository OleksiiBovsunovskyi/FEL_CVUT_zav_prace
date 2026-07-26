#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent; // xyz = tangent, w = bitangent sign

uniform mat4 uMVP;
uniform mat4 uPrevMVP;
uniform mat4 uModel;

out vec3 vFragPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vTexCoord;
out vec4 vCurrentClipPos;
out vec4 vPrevClipPos;

void main() {
    vec4 currentClip = uMVP     * vec4(aPosition, 1.0);
    vec4 prevClip = uPrevMVP * vec4(aPosition, 1.0);

    gl_Position = currentClip;
    vCurrentClipPos = currentClip;
    vPrevClipPos = prevClip;

    mat3 normalMatrix = mat3(transpose(inverse(uModel)));
    vFragPos = vec3(uModel * vec4(aPosition, 1.0));
    vNormal = normalMatrix * aNormal;
    vTangent = vec4(normalMatrix * aTangent.xyz, aTangent.w);
    vTexCoord = aTexCoord;
}

