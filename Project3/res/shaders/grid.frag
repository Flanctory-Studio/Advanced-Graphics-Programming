#version 330 core

uniform float left;
uniform float right;
uniform float bottom;
uniform float top;
uniform float znear;
uniform mat4 worldMatrix;
uniform mat4 viewMatrix;
uniform bool drawGrid;

uniform sampler2D worldPos;
uniform sampler2D finalText;

in vec2 texCoord;

out vec4 outColor;

const float eps = 0.0001;

float grid(vec3 worldPos, float gridStep)
{
    vec2 grid = fwidth(worldPos.xz) / mod(worldPos.xz, gridStep);
    return step(1.0,max(grid.x,grid.y));
}

void main()
{
    vec3 Position = texture(worldPos, texCoord).rgb;
    vec3 Final = texture(finalText, texCoord).rgb;

    if(Position.y <= 0.0 && drawGrid)
    {
        vec3 eyedirEyespace;
        eyedirEyespace.x = left + texCoord.x * (right - left);
        eyedirEyespace.y = bottom + texCoord.y * (top - bottom);
        eyedirEyespace.z = -znear;
        vec3 eyedirWorldspace = normalize(mat3(worldMatrix) * eyedirEyespace);

        vec3 eyeposEyespace = vec3(0.0,0.0,0.0);
        vec3 eyeposWorldspace = vec3(worldMatrix * vec4(eyeposEyespace, 1.0));

        vec3 planeNormalWorldspace = vec3(0.0, 1.0, 0.0);
        vec3 planePointWorldspace = vec3(0.0, 0.0, 0.0);

        float numerator = dot(planePointWorldspace - eyeposWorldspace, planeNormalWorldspace);
        float denominator = dot(eyedirWorldspace, planeNormalWorldspace) ;

        float t = numerator / denominator;

        if (t > 0.0)
        {
            vec3 hitWorldSpace = eyeposWorldspace + eyedirWorldspace * t;
            float color = grid(hitWorldSpace, 1.0);
            if (abs(color - 1.0) < eps)
            {
                outColor = vec4(color);
            }
            else
            {
                outColor = vec4(Final,1.0);
            }
        }
        else
        {
            gl_FragDepth = 0.0;
            outColor = vec4(Final,1.0);
        }
    }
    else
    {
        outColor = vec4(Final,1.0);
    }
}
