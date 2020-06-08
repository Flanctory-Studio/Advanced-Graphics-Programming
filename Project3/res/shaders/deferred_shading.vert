#version 330 core

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texCoords;
layout(location=3) in vec3 tangent;
layout(location=4) in vec3 bitangent;

uniform mat4 projectionMatrix;
uniform mat4 worldViewMatrix;
uniform vec3 uWorldPos;
uniform mat4 modelMatrix;

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
    pos = (worldViewMatrix * projectionMatrix * vec4(position, 1.0)).xyz;
    worldPos = vec4(uWorldPos + position, 1.0);
    gl_Position = projectionMatrix * worldViewMatrix * vec4(position, 1);


    // SSAO input textures
    vec4 mPos = worldViewMatrix * modelMatrix * vec4(position, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(worldViewMatrix * modelMatrix)));
    mNormal = normalMatrix * normal;
}
