#version 460

layout(location = 0) in MeshVertex {
    vec3 normal;
} v_in;

layout(location = 0) out vec4 outColor;

void main() {

    outColor = vec4(v_in.normal, 1.0);
}
