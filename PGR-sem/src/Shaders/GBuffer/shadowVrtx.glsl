#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uLightMVP;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = uLightMVP * vec4(aPosition, 1.0);
}

