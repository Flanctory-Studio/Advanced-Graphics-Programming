#version 330 core

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texCoords;
layout(location=3) in vec3 tangent;
layout(location=4) in vec3 bitangent;

uniform mat4 projectionMatrix;
uniform mat4 worldViewMatrix;

out vec2 vTexCoords;
out vec3 vNormal;
out vec3 pos;

void main(void)
{
    vTexCoords = texCoords;
    vNormal = normal;
    pos = position;
    gl_Position = projectionMatrix * worldViewMatrix * vec4(position, 1);
}
