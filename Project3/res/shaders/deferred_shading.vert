#version 330 core

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texCoords;
layout(location=3) in vec3 tangent;
layout(location=4) in vec3 bitangent;

uniform mat4 viewMatrix;
uniform mat3 normalMatrix;
uniform mat4 modelMatrix;
uniform mat4 projectionMatrix;
uniform vec3 uWorldPos;

out vec2 vTexCoords;
out vec3 vNormal;
out vec3 pos;
out vec4 worldPos;
out vec3 mPos;
out vec3 mNormal;

void main(void)
{
    vTexCoords = texCoords;
    vNormal = normal;
    pos = (vec4(position, 1.0)).xyz;
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1.0);
    worldPos = modelMatrix * vec4(position, 1.0);


    // SSAO input textures
    mPos = (viewMatrix * modelMatrix * vec4(position, 1.0)).xyz;

    mNormal = normalMatrix * normal;
}
