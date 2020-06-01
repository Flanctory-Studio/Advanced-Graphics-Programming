#version 330 core

uniform sampler2D colorTexture;
uniform sampler2D outlineTexture;
uniform bool blitAlpha;
uniform bool blitDepth;
uniform float outlineWidth;
uniform vec3 outlineColor;

in vec2 texCoord;

out vec4 outColor;

const float eps = 0.0001;


void main(void)
{
    vec4 texel = texture(colorTexture, texCoord);

        // if the pixel is outlineElement (we are on the silhouette)
    if (abs(texture(outlineTexture, texCoord).x - (1.0)) < eps)
        {
            vec2 size = 1.0f / textureSize(outlineTexture, 0);

            for (int i = -1; i <= +1; i++)
            {
                for (int j = -1; j <= +1; j++)
                {
                    if (i == 0 && j == 0)
                    {
                        continue;
                    }

                    vec2 offset = vec2(i, j) * size * outlineWidth;

                    // If one of the neighboring pixels is different to outlineElement (we are on the border)
                    if (abs(texture(outlineTexture, texCoord + offset).x - (1.0)) > eps)
                    {
                        texel = vec4(outlineColor, 1.0f);
                    }
                }
            }
         }



    if (blitAlpha) {
        outColor.rgb = vec3(texel.a);
    } else if (blitDepth) {
        float f = 10000.0;
        float n = 0.01;
        float z = abs((2 * f * n) / ((texel.r * 2.0 - 1.0) *(f-n)-(f+n)));
        outColor.rgb = vec3(z / 50.0);
    } else {
        outColor.rgb = texel.rgb;
    }

    // Gamma correction
    outColor = pow(outColor, vec4(1.0/2.2));
    outColor.a = 1.0;
}
