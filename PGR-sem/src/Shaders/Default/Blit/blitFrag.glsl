#version 450 core

in VS_OUT {
    vec2 texCoord;
} fs_in;

uniform sampler2D sourceTexture;

out vec4 FragColor;

void main()
{
    FragColor = texture(sourceTexture, fs_in.texCoord);
}
