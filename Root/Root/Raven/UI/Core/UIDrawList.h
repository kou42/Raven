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

enum class UILineCap
{
    Butt,
    Round,
    Square
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
    // open polylineではbutt / round / square capを端点へ適用し、closed polylineではcapを生成しません。
    // joinはsegment同士の重なりで接続する基礎実装とし、miter / round / bevelは後続拡張で追加します。
    void AddPolyline(
        const std::vector<math::Vec2>& points,
        float strokeWidth,
        const math::Vec4& color,
        bool closed = false,
        UILineCap lineCap = UILineCap::Butt)
    {
        if (points.empty() == true || strokeWidth <= 0.0f)
        {
            return;
        }

        constexpr float kSegmentEpsilonSquared = 0.000001f;
        constexpr std::size_t kRoundCapSegmentCount = 12u;
        constexpr float kPi = 3.14159265358979323846f;
        const float halfWidth = strokeWidth * 0.5f;

        // movetoだけのzero-length open subpathでもround/square capは可視になります。
        // buttは面積を持たないため何も生成しません。closed側はcap概念を持たないので同様に描画しません。
        if (points.size() == 1u)
        {
            if (closed == true || lineCap == UILineCap::Butt)
            {
                return;
            }

            const math::Vec2& center = points[0u];
            const math::Vec2 min(center.x - halfWidth, center.y - halfWidth);
            const math::Vec2 max(center.x + halfWidth, center.y + halfWidth);
            if (lineCap == UILineCap::Round)
            {
                AddCircle(min, max, color);
            }
            else
            {
                AddRect(min, max, color);
            }
            return;
        }

        const std::size_t segmentCount = closed == true ? points.size() : points.size() - 1u;
        std::size_t firstValidSegment = segmentCount;
        std::size_t lastValidSegment = segmentCount;
        for (std::size_t index = 0u; index < segmentCount; ++index)
        {
            const math::Vec2& start = points[index];
            const math::Vec2& end = points[(index + 1u) % points.size()];
            const float dx = end.x - start.x;
            const float dy = end.y - start.y;
            if (dx * dx + dy * dy > kSegmentEpsilonSquared)
            {
                if (firstValidSegment == segmentCount)
                {
                    firstValidSegment = index;
                }
                lastValidSegment = index;
            }
        }

        if (firstValidSegment == segmentCount)
        {
            return;
        }

        for (std::size_t index = 0u; index < segmentCount; ++index)
        {
            const math::Vec2& sourceStart = points[index];
            const math::Vec2& sourceEnd = points[(index + 1u) % points.size()];
            const float dx = sourceEnd.x - sourceStart.x;
            const float dy = sourceEnd.y - sourceStart.y;
            const float lengthSquared = dx * dx + dy * dy;
            if (lengthSquared <= kSegmentEpsilonSquared)
            {
                continue;
            }

            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            const math::Vec2 tangent(dx * inverseLength, dy * inverseLength);
            const math::Vec2 normal(-tangent.y * halfWidth, tangent.x * halfWidth);
            math::Vec2 start = sourceStart;
            math::Vec2 end = sourceEnd;

            // square capは線分方向へstroke半幅だけ延長した矩形として表現できます。
            if (closed == false && lineCap == UILineCap::Square)
            {
                if (index == firstValidSegment)
                {
                    start.x -= tangent.x * halfWidth;
                    start.y -= tangent.y * halfWidth;
                }
                if (index == lastValidSegment)
                {
                    end.x += tangent.x * halfWidth;
                    end.y += tangent.y * halfWidth;
                }
            }

            std::vector<math::Vec2> quad;
            quad.reserve(4u);
            quad.push_back(math::Vec2(start.x + normal.x, start.y + normal.y));
            quad.push_back(math::Vec2(end.x + normal.x, end.y + normal.y));
            quad.push_back(math::Vec2(end.x - normal.x, end.y - normal.y));
            quad.push_back(math::Vec2(start.x - normal.x, start.y - normal.y));
            AddPolygon(quad, color);
        }

        if (closed == true || lineCap != UILineCap::Round)
        {
            return;
        }

        auto addRoundCap = [this, &color, halfWidth](
            const math::Vec2& center,
            const math::Vec2& outwardTangent)
        {
            constexpr std::size_t roundCapSegmentCount = 12u;
            constexpr float pi = 3.14159265358979323846f;
            const float baseAngle = std::atan2(outwardTangent.y, outwardTangent.x) - pi * 0.5f;
            for (std::size_t index = 0u; index < roundCapSegmentCount; ++index)
            {
                const float t0 = static_cast<float>(index) / static_cast<float>(roundCapSegmentCount);
                const float t1 = static_cast<float>(index + 1u) / static_cast<float>(roundCapSegmentCount);
                const float angle0 = baseAngle + pi * t0;
                const float angle1 = baseAngle + pi * t1;

                std::vector<math::Vec2> triangle;
                triangle.reserve(3u);
                triangle.push_back(center);
                triangle.push_back(math::Vec2(
                    center.x + std::cos(angle0) * halfWidth,
                    center.y + std::sin(angle0) * halfWidth));
                triangle.push_back(math::Vec2(
                    center.x + std::cos(angle1) * halfWidth,
                    center.y + std::sin(angle1) * halfWidth));
                AddPolygon(triangle, color);
            }
        };

        const math::Vec2& firstStart = points[firstValidSegment];
        const math::Vec2& firstEnd = points[(firstValidSegment + 1u) % points.size()];
        const float firstDx = firstEnd.x - firstStart.x;
        const float firstDy = firstEnd.y - firstStart.y;
        const float firstInverseLength = 1.0f / std::sqrt(firstDx * firstDx + firstDy * firstDy);
        addRoundCap(
            firstStart,
            math::Vec2(-firstDx * firstInverseLength, -firstDy * firstInverseLength));

        const math::Vec2& lastStart = points[lastValidSegment];
        const math::Vec2& lastEnd = points[(lastValidSegment + 1u) % points.size()];
        const float lastDx = lastEnd.x - lastStart.x;
        const float lastDy = lastEnd.y - lastStart.y;
        const float lastInverseLength = 1.0f / std::sqrt(lastDx * lastDx + lastDy * lastDy);
        addRoundCap(
            lastEnd,
            math::Vec2(lastDx * lastInverseLength, lastDy * lastInverseLength));
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
