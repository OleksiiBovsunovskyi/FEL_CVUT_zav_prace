#version 140
in vec3 position;
in vec3 normal;
uniform mat4 uMVP;
out vec3 vNormal;
void main() {
  gl_Position = uMVP * vec4(position, 1.0);
  vNormal = normal;
}
