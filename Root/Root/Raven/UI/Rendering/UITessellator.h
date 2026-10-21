#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"

#include <cstdint>
#include <vector>

namespace Raven
{

// Backendへ渡すRaven UI共通頂点です。
// OpenGL / DX12 / Vulkanで同じCPU tessellation結果を使用し、形状生成の差をなくします。
struct UIVertex
{
    math::Vec2 Position{};
    math::Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec2 Texcoord{};
};

struct UITessellatedCommand
{
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;
    UIClipRect Clip{};
    UITextureView TextureView;
    bool UseTexture = false;
};

struct UITessellatedDrawList
{
    std::vector<UIVertex> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<UITessellatedCommand> Commands;

    bool IsEmpty() const
    {
        return Indices.empty();
    }
};

struct UITessellationMetrics
{
    std::size_t DrawCommandCount = 0u;
    std::size_t VertexCount = 0u;
    std::size_t IndexCount = 0u;
    std::size_t TextureSwitchCount = 0u;
};

// UIDrawListのRect/Circle/Polygon/ImageをBackend非依存のTriangle Listへ変換します。
// GPU Resource生成・Scissor座標変換・Texture Bindingは各Rendererが担当します。
class UITessellator
{
public:
    static bool Tessellate(
        const UIDrawList& drawList,
        UITessellatedDrawList& outDrawList);

    // GPU Bufferを生成せず、実際のTessellation規約と同じ頂点・Index数を集計します。
    // Texture切替は描画可能Command間で参照Resourceが変わった回数です。
    static UITessellationMetrics Measure(const UIDrawList& drawList);
};

} // namespace Raven
