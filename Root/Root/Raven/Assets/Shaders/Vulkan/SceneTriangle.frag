#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D albedoTexture;

// Vertexのmat4(64 byte)とoffsetを合わせ、MeshごとのTintとOpacityを受け取ります。
layout(push_constant) uniform MeshMaterial
{
    layout(offset = 64) vec4 Tint;
} meshMaterial;

void main()
{
    // 頂点色・Texture・Mesh別Tintを乗算し、Texture AlphaもBlendへ反映します。
    outColor = texture(albedoTexture, fragUV) *
        vec4(fragColor * meshMaterial.Tint.rgb, meshMaterial.Tint.a);
}
