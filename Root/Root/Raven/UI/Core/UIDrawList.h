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

enum class UILineJoin
{
    Miter,
    Round,
    Bevel
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
// fill-ruleやstroke join/capはGPU API固有ではないPath semanticsなので、この境界で解決します。
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
    // capはopen polylineの端点だけ、joinは前後に有効segmentを持つ頂点だけへ適用します。
    // miterLimitはSVG既定値4.0を既定引数にし、超過する鋭角はbevelへフォールバックします。
    void AddPolyline(
        const std::vector<math::Vec2>& points,
        float strokeWidth,
        const math::Vec4& color,
        bool closed = false,
        UILineCap lineCap = UILineCap::Butt,
        UILineJoin lineJoin = UILineJoin::Miter,
        float miterLimit = 4.0f)
    {
        if (points.empty() == true || strokeWidth <= 0.0f)
        {
            return;
        }

        constexpr float kSegmentEpsilonSquared = 0.000001f;
        constexpr float kDirectionEpsilon = 0.000001f;
        constexpr float kPi = 3.14159265358979323846f;
        constexpr std::size_t kRoundSegmentCount = 12u;
        const float halfWidth = strokeWidth * 0.5f;

        // movetoだけのzero-length open subpathでもround/square capは可視になります。
        // buttは面積を持たず、closed側にはcap概念がないため何も生成しません。
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
        auto isValidSegment = [&points, segmentCount, kSegmentEpsilonSquared](std::size_t segmentIndex)
        {
            if (segmentIndex >= segmentCount)
            {
                return false;
            }
            const math::Vec2& start = points[segmentIndex];
            const math::Vec2& end = points[(segmentIndex + 1u) % points.size()];
            const float dx = end.x - start.x;
            const float dy = end.y - start.y;
            return dx * dx + dy * dy > kSegmentEpsilonSquared;
        };

        std::size_t firstValidSegment = segmentCount;
        std::size_t lastValidSegment = segmentCount;
        for (std::size_t index = 0u; index < segmentCount; ++index)
        {
            if (isValidSegment(index) == true)
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

        // まず各segment本体を矩形へ展開します。joinはこの後で外側の隙間だけを追加geometryで埋めます。
        for (std::size_t index = 0u; index < segmentCount; ++index)
        {
            if (isValidSegment(index) == false)
            {
                continue;
            }

            const math::Vec2& sourceStart = points[index];
            const math::Vec2& sourceEnd = points[(index + 1u) % points.size()];
            const float dx = sourceEnd.x - sourceStart.x;
            const float dy = sourceEnd.y - sourceStart.y;
            const float inverseLength = 1.0f / std::sqrt(dx * dx + dy * dy);
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

        auto addRoundSector = [this, &color, halfWidth, kRoundSegmentCount](
            const math::Vec2& center,
            const math::Vec2& fromDirection,
            float sweepAngle)
        {
            const float startAngle = std::atan2(fromDirection.y, fromDirection.x);
            for (std::size_t index = 0u; index < kRoundSegmentCount; ++index)
            {
                const float t0 = static_cast<float>(index) / static_cast<float>(kRoundSegmentCount);
                const float t1 = static_cast<float>(index + 1u) / static_cast<float>(kRoundSegmentCount);
                const float angle0 = startAngle + sweepAngle * t0;
                const float angle1 = startAngle + sweepAngle * t1;

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

        // 各頂点で前後の有効segmentを探し、曲がりの外側だけをjoin geometryで埋めます。
        // zero-length segmentを挟む場合も、その前後の実segmentを使うことで不必要な穴を作りません。
        const std::size_t firstJoinVertex = closed == true ? 0u : 1u;
        const std::size_t endJoinVertex = closed == true ? points.size() : points.size() - 1u;
        for (std::size_t vertexIndex = firstJoinVertex; vertexIndex < endJoinVertex; ++vertexIndex)
        {
            std::size_t previousSegment = segmentCount;
            std::size_t nextSegment = segmentCount;

            if (closed == true)
            {
                for (std::size_t offset = 0u; offset < segmentCount; ++offset)
                {
                    const std::size_t candidate = (vertexIndex + segmentCount - 1u - offset) % segmentCount;
                    if (isValidSegment(candidate) == true)
                    {
                        previousSegment = candidate;
                        break;
                    }
                }
                for (std::size_t offset = 0u; offset < segmentCount; ++offset)
                {
                    const std::size_t candidate = (vertexIndex + offset) % segmentCount;
                    if (isValidSegment(candidate) == true)
                    {
                        nextSegment = candidate;
                        break;
                    }
                }
            }
            else
            {
                for (std::size_t candidate = vertexIndex; candidate > 0u; --candidate)
                {
                    if (isValidSegment(candidate - 1u) == true)
                    {
                        previousSegment = candidate - 1u;
                        break;
                    }
                }
                for (std::size_t candidate = vertexIndex; candidate < segmentCount; ++candidate)
                {
                    if (isValidSegment(candidate) == true)
                    {
                        nextSegment = candidate;
                        break;
                    }
                }
            }

            if (previousSegment == segmentCount ||
                nextSegment == segmentCount ||
                previousSegment == nextSegment)
            {
                continue;
            }

            const math::Vec2& previousStart = points[previousSegment];
            const math::Vec2& previousEnd = points[(previousSegment + 1u) % points.size()];
            const math::Vec2& nextStart = points[nextSegment];
            const math::Vec2& nextEnd = points[(nextSegment + 1u) % points.size()];
            const float previousDx = previousEnd.x - previousStart.x;
            const float previousDy = previousEnd.y - previousStart.y;
            const float nextDx = nextEnd.x - nextStart.x;
            const float nextDy = nextEnd.y - nextStart.y;
            const float previousInverseLength = 1.0f / std::sqrt(previousDx * previousDx + previousDy * previousDy);
            const float nextInverseLength = 1.0f / std::sqrt(nextDx * nextDx + nextDy * nextDy);
            const math::Vec2 previousTangent(
                previousDx * previousInverseLength,
                previousDy * previousInverseLength);
            const math::Vec2 nextTangent(
                nextDx * nextInverseLength,
                nextDy * nextInverseLength);
            const float turnCross =
                previousTangent.x * nextTangent.y - previousTangent.y * nextTangent.x;
            if (std::abs(turnCross) <= kDirectionEpsilon)
            {
                // 直進や完全な折り返しでは外側wedgeを一意に決められず、segment矩形だけで十分です。
                continue;
            }

            const float outerSign = turnCross > 0.0f ? -1.0f : 1.0f;
            const math::Vec2 previousOuterDirection(
                -previousTangent.y * outerSign,
                previousTangent.x * outerSign);
            const math::Vec2 nextOuterDirection(
                -nextTangent.y * outerSign,
                nextTangent.x * outerSign);
            const math::Vec2& center = points[vertexIndex];
            const math::Vec2 previousOuter(
                center.x + previousOuterDirection.x * halfWidth,
                center.y + previousOuterDirection.y * halfWidth);
            const math::Vec2 nextOuter(
                center.x + nextOuterDirection.x * halfWidth,
                center.y + nextOuterDirection.y * halfWidth);

            if (lineJoin == UILineJoin::Round)
            {
                const float outerCross =
                    previousOuterDirection.x * nextOuterDirection.y -
                    previousOuterDirection.y * nextOuterDirection.x;
                const float outerDot =
                    previousOuterDirection.x * nextOuterDirection.x +
                    previousOuterDirection.y * nextOuterDirection.y;
                const float sweepAngle = std::atan2(outerCross, outerDot);
                addRoundSector(center, previousOuterDirection, sweepAngle);
                continue;
            }

            if (lineJoin == UILineJoin::Miter)
            {
                math::Vec2 bisector(
                    previousOuterDirection.x + nextOuterDirection.x,
                    previousOuterDirection.y + nextOuterDirection.y);
                const float bisectorLengthSquared = bisector.x * bisector.x + bisector.y * bisector.y;
                if (bisectorLengthSquared > kSegmentEpsilonSquared)
                {
                    const float inverseBisectorLength = 1.0f / std::sqrt(bisectorLengthSquared);
                    bisector.x *= inverseBisectorLength;
                    bisector.y *= inverseBisectorLength;
                    const float projection =
                        bisector.x * previousOuterDirection.x +
                        bisector.y * previousOuterDirection.y;
                    if (projection > kDirectionEpsilon)
                    {
                        const float miterLength = halfWidth / projection;
                        const float safeMiterLimit = miterLimit > 1.0f ? miterLimit : 1.0f;
                        if (miterLength <= halfWidth * safeMiterLimit)
                        {
                            const math::Vec2 miterPoint(
                                center.x + bisector.x * miterLength,
                                center.y + bisector.y * miterLength);
                            // segment矩形の間に残るcenter側の三角領域も含め、外側wedge全体を4頂点で埋めます。
                            AddPolygon(
                                std::vector<math::Vec2>{ center, previousOuter, miterPoint, nextOuter },
                                color);
                            continue;
                        }
                    }
                }
            }

            // bevel、またはmiterlimitを超えたmiterは外側2点を直線で結びます。
            AddPolygon(
                std::vector<math::Vec2>{ center, previousOuter, nextOuter },
                color);
        }

        if (closed == true || lineCap != UILineCap::Round)
        {
            return;
        }

        const math::Vec2& firstStart = points[firstValidSegment];
        const math::Vec2& firstEnd = points[(firstValidSegment + 1u) % points.size()];
        const float firstDx = firstEnd.x - firstStart.x;
        const float firstDy = firstEnd.y - firstStart.y;
        const float firstInverseLength = 1.0f / std::sqrt(firstDx * firstDx + firstDy * firstDy);
        const math::Vec2 firstOutward(-firstDx * firstInverseLength, -firstDy * firstInverseLength);
        const math::Vec2 firstFrom(firstOutward.y, -firstOutward.x);
        addRoundSector(firstStart, firstFrom, kPi);

        const math::Vec2& lastStart = points[lastValidSegment];
        const math::Vec2& lastEnd = points[(lastValidSegment + 1u) % points.size()];
        const float lastDx = lastEnd.x - lastStart.x;
        const float lastDy = lastEnd.y - lastStart.y;
        const float lastInverseLength = 1.0f / std::sqrt(lastDx * lastDx + lastDy * lastDy);
        const math::Vec2 lastOutward(lastDx * lastInverseLength, lastDy * lastInverseLength);
        const math::Vec2 lastFrom(lastOutward.y, -lastOutward.x);
        addRoundSector(lastEnd, lastFrom, kPi);
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
