#version 330 core

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texCoords;
layout(location=3) in vec3 tangent;
layout(location=4) in vec3 bitangent;

out vec2 TexCoords;

void main()
{
    TexCoords = position.xy * 0.5 + vec2(0.5);
    gl_Position = vec4(position, 1.0);
}
