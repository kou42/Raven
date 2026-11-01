#version 450

// SceneTriangle.vertと同じ80byteのPush Constant範囲を共有します。
// Tint先頭と重なるoffset 64にはPicking Pass中だけEntityIndexをintで設定します。
layout(push_constant) uniform EntityPicking
{
    layout(offset = 64) int EntityIndex;
} picking;
layout(location = 0) out int outEntityIndex;

void main()
{
    outEntityIndex = picking.EntityIndex;
}
