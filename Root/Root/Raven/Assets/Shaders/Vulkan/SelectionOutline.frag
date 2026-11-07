#version 450

layout(location = 0) out vec4 outColor;
layout(push_constant) uniform OutlineMaterial
{
    layout(offset = 64) vec4 Tint;
} outlineMaterial;

void main()
{
    // 頂点色/Textureに影響されず、Editorが指定した選択色だけを出力します。
    outColor = outlineMaterial.Tint;
}
