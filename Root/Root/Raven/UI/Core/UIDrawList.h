#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

#include <cstddef>
#include <vector>

namespace Raven
{

class TextureAsset;

enum class UIDrawCommandType
{
    SolidRect,
    SolidCircle,
    SolidPolygon,
    Image
};

enum class UIFillRule
{
    NonZero,
    EvenOdd
};

struct UIRect
{
    math::Vec2 Min{};
    math::Vec2 Max{};
};

struct UIClipRect
{
    UIRect Rect{};
    bool Enabled = false;

    static UIClipRect Disabled();
    static UIClipRect FromRect(const UIRect& rect);
    static UIClipRect Intersect(const UIClipRect& inheritedClip, const UIRect& rect);

    bool Contains(const math::Vec2& point) const;
};

struct UITransform2D
{
    float M00 = 1.0f;
    float M01 = 0.0f;
    float M10 = 0.0f;
    float M11 = 1.0f;
    math::Vec2 Translation{};

    static UITransform2D Identity();

    static UITransform2D CreateScaleRotation(
        const math::Vec2& pivot,
        float rotation,
        const math::Vec2& scale);

    static UITransform2D Combine(
        const UITransform2D& parent,
        const UITransform2D& local);

    math::Vec2 TransformPoint(const math::Vec2& point) const;
    UIRect TransformRectBounds(const UIRect& rect) const;

    bool TryInverseTransformPoint(
        const math::Vec2& point,
        math::Vec2& outPoint) const;
};

struct UIDrawCommand
{
    UIDrawCommandType Type = UIDrawCommandType::SolidRect;
    UIRect Rect{};
    UITransform2D Transform{};
    UIClipRect Clip{};
    math::Vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec2 UVMin{ 0.0f, 0.0f };
    math::Vec2 UVMax{ 1.0f, 1.0f };
    std::vector<math::Vec2> Points;
    Ref<TextureAsset> Texture;
};

// 1 UI frame分の描画要求をCPU側へ蓄積します。
// 通常のsimple polygonは従来どおりRenderer backendで三角形化します。
// Compound polygonだけはfill-ruleを解決するため、DrawListで輪郭関係を解析してTriangle単位へ正規化します。
// fill-ruleはGPU API固有ではないPath semanticsなので、この境界で解決することでSVG固有知識をRendererへ持ち込みません。
class UIDrawList
{
public:
    void Clear();

    void AddRect(
        const math::Vec2& min,
        const math::Vec2& max,
        const math::Vec4& color);

    void AddCircle(
        const math::Vec2& min,
        const math::Vec2& max,
        const math::Vec4& color);

    void AddPolygon(
        const std::vector<math::Vec2>& points,
        const math::Vec4& color);

    // 複数の閉輪郭をnonzero/evenodd規則で1つの塗り領域として解釈します。
    // 穴を含むためsimple polygon commandへ直接表現できず、CPU側でfilled regionをTriangleへ分解して追加します。
    void AddCompoundPolygon(
        const std::vector<std::vector<math::Vec2>>& contours,
        UIFillRule fillRule,
        const math::Vec4& color);

    void AddImage(
        const math::Vec2& min,
        const math::Vec2& max,
        const Ref<TextureAsset>& texture,
        const math::Vec4& tintColor = math::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f },
        const math::Vec2& uvMin = math::Vec2{ 0.0f, 0.0f },
        const math::Vec2& uvMax = math::Vec2{ 1.0f, 1.0f });

    void ApplyTransform(std::size_t firstCommand, const UITransform2D& transform);
    void ApplyClip(std::size_t firstCommand, const UIClipRect& clip);

    const std::vector<UIDrawCommand>& GetCommands() const;
    std::size_t GetCommandCount() const;
    bool IsEmpty() const;

private:
    std::vector<UIDrawCommand> m_Commands;
};

} // namespace Raven
