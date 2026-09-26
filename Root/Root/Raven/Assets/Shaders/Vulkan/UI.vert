#version 450
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;
layout(push_constant) uniform UIConstants
{
    mat4 clipTransform;
    vec4 tint;
} constants;
void main()
{
    gl_Position = constants.clipTransform * vec4(inPosition, 0.0, 1.0);
    outColor = inColor;
    outTexCoord = inTexCoord;
}
