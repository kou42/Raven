#pragma once

#include "Raven/UI/Rendering/UITessellator.h"

namespace Raven
{
// 左上原点のFramebuffer Pixel矩形。GLへのY反転だけはBackend境界で行います。
struct UIPixelClip
{
    uint32_t X = 0u;
    uint32_t Y = 0u;
    uint32_t Width = 0u;
    uint32_t Height = 0u;
    bool operator==(const UIPixelClip&) const = default;
};

struct UIRenderBatch
{
    uint32_t FirstIndex = 0u;
    uint32_t IndexCount = 0u;
    UIPixelClip Clip;
    UITextureView TextureView;
    bool UseTexture = false;
};

// CPU Geometryと描画順を確定したBatchです。TextureViewのRefはSubmit終了まで保持します。
// GPU Buffer/Descriptorの所有権とFence待ちは各Renderer/Frame Lifecycleに残します。
struct UIRenderGeometry
{
    std::vector<UIVertex> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<UIRenderBatch> Batches;
    std::size_t SourceCommandCount = 0u;
    std::size_t DrawableCommandCount = 0u;
};

class UIGeometryBuilder
{
public:
    // logicalSizeはDrawListの座標単位、Framebufferは実Pixel数。Content Scaleとは別の倍率です。
    // 空/全Clip外は成功した空Batch、無効な寸法/非有限Clipはfalse。GPU stateは変更しません。
    // mergeAdjacent=falseは同じGeometryを使う比較計測用で、並べ替えはどちらも行いません。
    static bool Build(const UIDrawList& drawList, const math::Vec2& logicalSize,
        uint32_t framebufferWidth, uint32_t framebufferHeight,
        UIRenderGeometry& outGeometry, bool mergeAdjacent = true);
};
} // namespace Raven
