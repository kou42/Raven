#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

// Vertexのmat4(64 byte)とoffsetを合わせ、MeshごとのTintとOpacityを受け取ります。
layout(push_constant) uniform MeshMaterial
{
    layout(offset = 64) vec4 Tint;
} meshMaterial;

void main()
{
    outColor = vec4(fragColor * meshMaterial.Tint.rgb, meshMaterial.Tint.a);
}
