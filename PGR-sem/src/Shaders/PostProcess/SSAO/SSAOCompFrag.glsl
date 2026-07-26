#version 330 core

in  vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uColor;   // scene colour
uniform sampler2D uSSAO;    // blurred occlusion factor [0, 1]
uniform float     uStrength; // power curve exponent (>1 darkens more)

void main() {
    vec3  color = texture(uColor, vTexCoord).rgb;
    float ao = texture(uSSAO,  vTexCoord).r;

    // Power curve: values < 1 get darkened more strongly as uStrength increases.
    ao = pow(ao, uStrength);

    fragColor = vec4(color * ao, 1.0);
}

