#version 330 core

in vec2 TexCoords;

out vec4 outColor;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gSSAO;

uniform vec3 lightPositions[8];
uniform vec3 lightColors[8];
uniform float lightIntensity[8];
uniform float lightRange[8];

float linear = 0.7;
float quadratic = 1.8;

uniform vec3 viewPos;
uniform vec3 backgroundColor;

void main()
{
    // retrieve data from gbuffer
    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec3 Normal = (texture(gNormal, TexCoords).rgb - vec3(0.5)) * 2.0;
    vec3 Diffuse = texture(gAlbedoSpec, TexCoords).rgb;
    float Specular = texture(gAlbedoSpec, TexCoords).a;
    float AmbientOcclusion = texture(gSSAO, TexCoords).r;


    vec3 lighting = backgroundColor;

    if (length(Normal) >= 0.9 && length(Normal) <= 1.1)
    {    // then calculate lighting as usual
        lighting  = vec3(Diffuse * 0.1 * AmbientOcclusion); // hard-coded ambient component
        vec3 viewDir  = normalize(viewPos - FragPos);
        for(int i = 0; i < 8; ++i)
        {
            float distance = length(lightPositions[i] - FragPos);
            if (distance <= lightRange[i])
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
                lighting += (diffuse + specular) * lightIntensity[i];
            }
        }
    }

    outColor = vec4(lighting, 1.0);


    //outColor = vec4(lightPositions[0], 1.0);

}
