#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent; 

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vFragPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vTexCoord;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    mat3 normalMatrix = mat3(transpose(inverse(uModel)));
    vFragPos = vec3(uModel * vec4(aPosition, 1.0));
    vNormal = normalMatrix * aNormal;
    vTangent = vec4(normalMatrix * aTangent.xyz, aTangent.w);
    vTexCoord = aTexCoord;
}

