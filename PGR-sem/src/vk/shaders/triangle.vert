#version 460

// Vulkan clip-space convention: +Y points DOWN, depth is [0,1].
// So the apex below has a negative Y to appear at the top of the window.

layout(location = 0) out vec3 vColor;

void main() {
    const vec2 positions[3] = vec2[3](
        vec2( 0.0, -0.6),   // top
        vec2(-0.6,  0.4),   // bottom-left
        vec2( 0.6,  0.4)    // bottom-right
    );
    const vec3 colors[3] = vec3[3](
        vec3(1.0, 0.25, 0.25),
        vec3(0.25, 1.0, 0.35),
        vec3(0.3, 0.45, 1.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vColor      = colors[gl_VertexIndex];
}
