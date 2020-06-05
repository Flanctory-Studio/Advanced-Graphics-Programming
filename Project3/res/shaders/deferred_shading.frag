#version 330 core

uniform sampler2D albedoTexture;
uniform sampler2D specularTexture;
uniform float selectionColor;

in vec2 vTexCoords;
in vec3 vNormal;
in vec3 pos;

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormals;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec4 outSelection;
layout (location = 4) out vec4 fragmentdepth;


float near = 0.1;
float far  = 100.0;

float LinearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; // back to NDC
    return (2.0 * near * far) / (far + near - z * (far - near));
}

void main(void)
{
    outPosition = vec4(pos, 1.0);
    outNormals = vec4(vNormal * 0.5 + vec3(0.5), 1.0);
    outAlbedo.rgb = texture(albedoTexture, vTexCoords).rgb;
    outAlbedo.a = texture(specularTexture, vTexCoords).r;

    outSelection = vec4(selectionColor);

    float depth = LinearizeDepth(gl_FragCoord.z) / far;
    fragmentdepth = vec4(vec3(depth), 1.0);
}
