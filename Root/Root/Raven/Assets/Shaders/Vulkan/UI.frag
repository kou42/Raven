#version 450
layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(push_constant) uniform UIConstants
{
    mat4 clipTransform;
    vec4 tint;
} constants;
void main()
{
    outColor = inColor * texture(uTexture, inTexCoord) * constants.tint;
}
