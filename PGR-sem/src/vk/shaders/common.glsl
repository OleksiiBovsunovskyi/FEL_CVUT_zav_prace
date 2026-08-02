
#ifndef COMMON_GLSL_INCLUDED
#define COMMON_GLSL_INCLUDED

const float PI     = 3.14159265359;
const float TWO_PI = 6.28318530718;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  saturate(vec3  v) { return clamp(v, vec3(0.0), vec3(1.0)); }

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

#endif // COMMON_GLSL_INCLUDED
