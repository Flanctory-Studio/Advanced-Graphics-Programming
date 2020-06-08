#version 330 core

layout(location=0) in vec3 position;

out vec2 TexCoords;

void main()
{
    TexCoords = position.xy * 0.5 + vec2(0.5);
    gl_Position = vec4(position, 1.0);
}
