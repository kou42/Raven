#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"

#include <cstdint>
#include <vector>

namespace Raven
{

class TextureAsset;

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
    Ref<TextureAsset> Texture;
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

// UIDrawListのRect/Circle/Polygon/ImageをBackend非依存のTriangle Listへ変換します。
// GPU Resource生成・Scissor座標変換・Texture Bindingは各Rendererが担当します。
class UITessellator
{
public:
    static bool Tessellate(
        const UIDrawList& drawList,
        UITessellatedDrawList& outDrawList);
};

} // namespace Raven
