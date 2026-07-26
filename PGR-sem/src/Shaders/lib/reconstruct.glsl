
/**
* Reconstruct world position from depth buffer
*/
vec3 reconstructWorldPos(vec2 uv, float depth, mat4 invViewProj) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = invViewProj * ndc;
    return worldPos.xyz / worldPos.w;
}
