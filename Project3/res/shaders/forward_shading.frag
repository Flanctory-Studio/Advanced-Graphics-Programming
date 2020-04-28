#version 330 core

// Matrices
uniform mat4 worldViewMatrix;
uniform mat3 normalMatrix;

// Material
uniform vec4 albedo;
uniform vec4 specular;
uniform vec4 emissive;
uniform float smoothness;
uniform float bumpiness;
uniform sampler2D albedoTexture;
uniform sampler2D specularTexture;
uniform sampler2D emissiveTexture;
uniform sampler2D normalTexture;
uniform sampler2D bumpTexture;

// Lights
#define MAX_LIGHTS 8
uniform int lightType[MAX_LIGHTS];
uniform vec3 lightPosition[MAX_LIGHTS];
uniform vec3 lightDirection[MAX_LIGHTS];
uniform vec3 lightColor[MAX_LIGHTS];
uniform int lightCount;

in vec2 vTexCoords;

in vec3 vNormal;

in vec3 pos;

out vec4 outColor;

void main(void)
{
    // TODO: Local illumination
    // Ambient
    float ambientStrength = 0.1;
    vec3 ambient = vec3(0.0);

    for (int i = 0; i < lightCount; i++)
    {
        ambient += lightColor[i];
    }
    ambient *= (1.0/lightCount);
    ambient *= ambientStrength;

    // Diffuse
    vec3 norm = normalize(vNormal);
    vec3 diffuse = vec3(0.0);

    for (int i = 0; i < lightCount; i++)
    {
        vec3 lightDir = normalize(pos- lightPosition[i]);
        float diff = max(dot(norm, lightDir), 0.0);
        diffuse += diff * lightColor[i];
    }
    diffuse *= (1.0/lightCount);

    // Specular
    float specularStrength = 0.5;
    vec3 cameraPos = worldViewMatrix[3].xyz;
    vec3 viewDir = normalize(cameraPos - pos);
    vec3 _specular = vec3(0.0);

    for (int i = 0; i < lightCount; i++)
    {
        vec3 reflectDir = reflect(-lightDirection[i], norm);
       float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
       _specular += specularStrength * spec * lightColor[i];
    }
    _specular *= (1.0/lightCount);

    _specular = clamp(_specular, vec3(0.0), vec3(1.0));


    outColor = vec4((ambient + diffuse + _specular) * texture(albedoTexture, vTexCoords).rgb, 1.0);
}
