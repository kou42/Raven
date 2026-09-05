#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

#include <cmath>
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

    // Polylineの各segmentをstroke-width分だけ法線方向へ押し出し、既存SolidPolygonへ正規化します。
    // 現段階のcapはSVG既定のbuttです。joinはsegment同士の重なりで接続する基礎実装とし、
    // miter / round / bevelの厳密な形状は後続拡張でこのAPIへ追加します。
    void AddPolyline(
        const std::vector<math::Vec2>& points,
        float strokeWidth,
        const math::Vec4& color,
        bool closed = false)
    {
        if (points.size() < 2u || strokeWidth <= 0.0f)
        {
            return;
        }

        const float halfWidth = strokeWidth * 0.5f;
        const std::size_t segmentCount = closed == true ? points.size() : points.size() - 1u;
        for (std::size_t index = 0u; index < segmentCount; ++index)
        {
            const math::Vec2& start = points[index];
            const math::Vec2& end = points[(index + 1u) % points.size()];
            const float dx = end.x - start.x;
            const float dy = end.y - start.y;
            const float lengthSquared = dx * dx + dy * dy;
            if (lengthSquared <= 0.000001f)
            {
                continue;
            }

            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            const math::Vec2 normal(-dy * inverseLength * halfWidth, dx * inverseLength * halfWidth);
            std::vector<math::Vec2> quad;
            quad.reserve(4u);
            quad.push_back(math::Vec2(start.x + normal.x, start.y + normal.y));
            quad.push_back(math::Vec2(end.x + normal.x, end.y + normal.y));
            quad.push_back(math::Vec2(end.x - normal.x, end.y - normal.y));
            quad.push_back(math::Vec2(start.x - normal.x, start.y - normal.y));
            AddPolygon(quad, color);
        }
    }

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
