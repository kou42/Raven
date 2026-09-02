#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

#include <cstddef>
#include <vector>

namespace Raven
{

class TextureAsset;

// ============================================================================
// UIDrawCommandType
// ============================================================================
// UIContextが生成する描画要求の種類です。
//
// SolidRectに加えてImageを扱いますが、UIElement側へGPU実装を漏らさない方針は維持します。
// Text / Border等も今後このコマンド列へ追加していきます。
// ImageもGPU Texture IDではなくTextureAssetを参照し、UI層からRenderer API固有値を排除します。
enum class UIDrawCommandType
{
    SolidRect,
    Image
};

// ============================================================================
// UIRect
// ============================================================================
// UI座標系上の矩形です。
// Minを左上、Maxを右下として扱い、座標単位はpixelを基本とします。
struct UIRect
{
    math::Vec2 Min{};
    math::Vec2 Max{};
};

// ============================================================================
// UITransform2D
// ============================================================================
// Layoutで確定した矩形へ後段で適用するVisual Transformです。
// Rotationはradian、Scaleは各軸倍率、PivotはElement local sizeに対するnormalized座標です。
// LayoutのDesiredSize / Arrange結果を変化させないため、Animationで回転・拡縮しても兄弟配置は揺れません。
struct UITransform2D
{
    float Rotation = 0.0f;
    math::Vec2 Scale{ 1.0f, 1.0f };
    math::Vec2 Pivot{ 0.5f, 0.5f };
};

// ============================================================================
// UIDrawCommand
// ============================================================================
// UI Tree / WidgetからRenderer backendへ渡す、GPU API非依存の描画要求です。
//
// 重要:
// この構造体へOpenGLのTexture IDやVertexArray等を直接持たせないことで、
// Editor UIとGame UIのどちらから利用してもPlatform Rendererへ依存しない境界を保ちます。
// ImageではEngine側のTextureAssetを保持し、OpenGL固有値への解決はUIRenderer実装でのみ行います。
// TextureAssetのRefをframe中保持することで、DrawListが参照しているRuntime Textureの寿命も保証します。
//
// Raven UIのnormalized UVは左上原点です。
// UV=(0, 0)を画像左上、UV=(1, 1)を画像右下とし、Vは下方向へ増加します。
// OpenGL等のnative Texture座標との差はUIRenderer backendが変換するため、Widget側ではAPI差を扱いません。
struct UIDrawCommand
{
    UIDrawCommandType Type = UIDrawCommandType::SolidRect;
    UIRect Rect{};
    UITransform2D Transform{};
    math::Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec2 UVMin{ 0.0f, 0.0f };
    math::Vec2 UVMax{ 1.0f, 1.0f };
    Ref<TextureAsset> Texture;
};

// ============================================================================
// UIDrawList
// ============================================================================
// 1 UI frame分の描画要求をCPU側へ蓄積します。
//
// ImGuiと同様に最終的な描画データはframeごとに再構築しますが、UIElementそのものは
// Retained Modeとして別途保持できるよう、DrawListはUI Treeの所有権を一切持ちません。
// これにより、将来のLayout / HitTest / Event処理とRenderingを分離できます。
class UIDrawList
{
public:
    void Clear();

    void AddRect(
        const math::Vec2& min,
        const math::Vec2& max,
        const math::Vec4& color);

    // uvMin / uvMaxもRaven UIの左上原点UV規約で指定します。
    // 画像全体を表示する既定値は左上(0, 0)から右下(1, 1)です。
    void AddImage(
        const math::Vec2& min,
        const math::Vec2& max,
        const Ref<TextureAsset>& texture,
        const math::Vec4& tintColor = math::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f },
        const math::Vec2& uvMin = math::Vec2{ 0.0f, 0.0f },
        const math::Vec2& uvMax = math::Vec2{ 1.0f, 1.0f });

    // 直前に追加されたCommandへElementのVisual Transformを合成します。
    // WidgetのOnBuildDrawList()が複数Commandを追加する場合にも対応できるよう、範囲指定で適用します。
    void ApplyTransform(std::size_t firstCommand, const UITransform2D& transform);

    const std::vector<UIDrawCommand>& GetCommands() const;
    std::size_t GetCommandCount() const;
    bool IsEmpty() const;

private:
    std::vector<UIDrawCommand> m_Commands;
};

} // namespace Raven
