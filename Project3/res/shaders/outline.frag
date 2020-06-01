#version 330 core

uniform sampler2D albedoTexture;
uniform sampler2D specularTexture;
uniform bool selected;
uniform float selectionColor;

in vec2 vTexCoords;
in vec3 vNormal;
in vec3 pos;

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormals;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec4 outSelection;

void main(void)
{
    outPosition = vec4(pos, 1.0);
    outNormals = vec4(vNormal * 0.5 + vec3(0.5), 1.0);
    outAlbedo.rgb = texture(albedoTexture, vTexCoords).rgb;
    outAlbedo.a = texture(specularTexture, vTexCoords).r;

    outSelection = vec4(selectionColor);
}
