#version 330 core

in vec2 TexCoords;

out vec4 outColor;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;

uniform vec3 lightPositions[8];
uniform vec3 lightColors[8];

float linear = 0.7;
float quadratic = 1.8;

uniform vec3 viewPos;

void main()
{
    // retrieve data from gbuffer
    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec3 Normal = texture(gNormal, TexCoords).rgb;
    vec3 Diffuse = texture(gAlbedoSpec, TexCoords).rgb;
    float Specular = texture(gAlbedoSpec, TexCoords).a;

    // then calculate lighting as usual
    vec3 lighting  = Diffuse * 0.1; // hard-coded ambient component
    vec3 viewDir  = normalize(viewPos - FragPos);
    for(int i = 0; i < 8; ++i)
    {
        // diffuse
        vec3 lightDir = normalize(lightPositions[i] - FragPos);
        vec3 diffuse = max(dot(Normal, lightDir), 0.0) * Diffuse * lightColors[i];
        // specular
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(Normal, halfwayDir), 0.0), 16.0);
        vec3 specular = lightColors[i] * spec * Specular;
        // attenuation
        float distance = length(lightPositions[i] - FragPos);
        float attenuation = 1.0 / (1.0 + linear * distance + quadratic * distance * distance);
        diffuse *= attenuation;
        specular *= attenuation;
        lighting += diffuse + specular;
    }
    outColor = vec4(lighting, 1.0);


    //outColor = vec4(lightPositions[0], 1.0);

}
