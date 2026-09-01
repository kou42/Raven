#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

#include <cstddef>
#include <vector>

namespace Raven
{

class TextureAsset;

// UIContextが生成する描画要求の種類です。
// ImageもGPU Texture IDではなくTextureAssetを参照し、UI層からRenderer API固有値を排除します。
enum class UIDrawCommandType
{
    SolidRect,
    Image
};

// UI座標系上の矩形です。Minを左上、Maxを右下として扱います。
struct UIRect
{
    math::Vec2 Min{};
    math::Vec2 Max{};
};

// UI Tree / WidgetからRenderer backendへ渡すGPU API非依存の描画要求です。
// TextureAssetのRefをframe中保持することで、DrawListが参照しているRuntime Textureの寿命も保証します。
struct UIDrawCommand
{
    UIDrawCommandType Type = UIDrawCommandType::SolidRect;
    UIRect Rect{};
    math::Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec2 UVMin{ 0.0f, 0.0f };
    math::Vec2 UVMax{ 1.0f, 1.0f };
    Ref<TextureAsset> Texture;
};

class UIDrawList
{
public:
    void Clear();

    void AddRect(
        const math::Vec2& min,
        const math::Vec2& max,
        const math::Vec4& color);

    void AddImage(
        const math::Vec2& min,
        const math::Vec2& max,
        const Ref<TextureAsset>& texture,
        const math::Vec4& tintColor = math::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f },
        const math::Vec2& uvMin = math::Vec2{ 0.0f, 0.0f },
        const math::Vec2& uvMax = math::Vec2{ 1.0f, 1.0f });

    const std::vector<UIDrawCommand>& GetCommands() const;
    std::size_t GetCommandCount() const;
    bool IsEmpty() const;

private:
    std::vector<UIDrawCommand> m_Commands;
};

} // namespace Raven
