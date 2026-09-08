#version 330 core

out vec4 o_Color;

void main()
{
    // 背景・既存Terrain Materialと区別しやすい固定Magentaで出力します。
    // この色が見えればTexture/Normal/Lightingは原因候補から外せます。
    o_Color = vec4(1.0, 0.0, 1.0, 1.0);
}
