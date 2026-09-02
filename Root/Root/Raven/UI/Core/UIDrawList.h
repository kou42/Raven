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
// UIClipRect
// ============================================================================
// Draw Commandへ継承するscreen-spaceのaxis-aligned Clipです。
// OpenGL Scissorへ直接変換できる表現に限定し、回転・ShearしたElementのClipは
// Transform後4頂点を包含するAABBとして扱います。
struct UIClipRect
{
    UIRect Rect{};
    bool Enabled = false;

    static UIClipRect Disabled();
    static UIClipRect FromRect(const UIRect& rect);
    static UIClipRect Intersect(const UIClipRect& inheritedClip, const UIRect& rect);

    bool Contains(const math::Vec2& point) const;
};

// ============================================================================
// UITransform2D
// ============================================================================
// Layout後のUI座標へ適用する2D Affine Transformです。
// 2x2線形部 + Translationで保持するため、親の非一様Scaleと子Rotationを合成した際に生じる
// Shear成分も失わず表現できます。LayoutのDesiredSize / Arrange結果自体は変更しません。
struct UITransform2D
{
    float M00 = 1.0f;
    float M01 = 0.0f;
    float M10 = 0.0f;
    float M11 = 1.0f;
    math::Vec2 Translation{};

    static UITransform2D Identity();

    // pivotを画面上の不変点として、Scale -> Rotationの順に適用するTransformを生成します。
    static UITransform2D CreateScaleRotation(
        const math::Vec2& pivot,
        float rotation,
        const math::Vec2& scale);

    // parent(local(point)) の順で適用されるWorld Transformを返します。
    static UITransform2D Combine(
        const UITransform2D& parent,
        const UITransform2D& local);

    math::Vec2 TransformPoint(const math::Vec2& point) const;

    // Rectの4頂点をTransformし、それらを包含するscreen-space AABBを返します。
    // Scissorはaxis-aligned矩形しか表現できないため、回転・Shear Clipの保守的境界として利用します。
    UIRect TransformRectBounds(const UIRect& rect) const;

    // Screen/World座標をTransform適用前の座標へ戻します。
    // 行列式0のTransformは逆変換できないためfalseを返します。
    bool TryInverseTransformPoint(
        const math::Vec2& point,
        math::Vec2& outPoint) const;
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
    UIClipRect Clip{};
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

    // 直前に追加されたCommandへElementのWorld Visual Transformを付与します。
    // WidgetのOnBuildDrawList()が複数Commandを追加する場合にも対応できるよう、範囲指定で適用します。
    void ApplyTransform(std::size_t firstCommand, const UITransform2D& transform);

    // 直前に追加されたCommandへAncestorから継承したClipを付与します。
    // Element自身のClipChildrenはSelf描画ではなくDescendantへ適用するため、呼び出し側で適用範囲を分けます。
    void ApplyClip(std::size_t firstCommand, const UIClipRect& clip);

    const std::vector<UIDrawCommand>& GetCommands() const;
    std::size_t GetCommandCount() const;
    bool IsEmpty() const;

private:
    std::vector<UIDrawCommand> m_Commands;
};

} // namespace Raven
