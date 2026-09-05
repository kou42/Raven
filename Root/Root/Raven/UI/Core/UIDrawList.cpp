#include "Raven/UI/Core/UIDrawList.h"

#include "Raven/Assets/TextureAsset.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Raven
{

namespace
{
constexpr float kCompoundPolygonEpsilon = 0.00001f;
constexpr float kCompoundPolygonEpsilonSquared =
    kCompoundPolygonEpsilon * kCompoundPolygonEpsilon;

struct CompoundScanEdge
{
    math::Vec2 Start{};
    math::Vec2 End{};
    int WindingDelta = 0;
};

struct ActiveScanEdge
{
    const CompoundScanEdge* Edge = nullptr;
    float MiddleX = 0.0f;
};

float DistanceSquared(const math::Vec2& left, const math::Vec2& right)
{
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    return dx * dx + dy * dy;
}

bool IsSamePoint(const math::Vec2& left, const math::Vec2& right)
{
    return DistanceSquared(left, right) <= kCompoundPolygonEpsilonSquared;
}

float Cross2D(
    const math::Vec2& a,
    const math::Vec2& b,
    const math::Vec2& c)
{
    return (b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x);
}

std::vector<math::Vec2> NormalizeContour(const std::vector<math::Vec2>& input)
{
    std::vector<math::Vec2> result;
    result.reserve(input.size());
    for (const math::Vec2& point : input)
    {
        if (result.empty() == true || IsSamePoint(result.back(), point) == false)
        {
            result.push_back(point);
        }
    }

    if (result.size() >= 2u && IsSamePoint(result.front(), result.back()) == true)
    {
        result.pop_back();
    }
    return result;
}

float EvaluateEdgeX(const CompoundScanEdge& edge, float y)
{
    const float deltaY = edge.End.y - edge.Start.y;
    if (std::abs(deltaY) <= kCompoundPolygonEpsilon)
    {
        return edge.Start.x;
    }

    const float t = (y - edge.Start.y) / deltaY;
    return edge.Start.x + (edge.End.x - edge.Start.x) * t;
}

bool TryFindProperIntersectionY(
    const CompoundScanEdge& left,
    const CompoundScanEdge& right,
    float& outY)
{
    const float leftDx = left.End.x - left.Start.x;
    const float leftDy = left.End.y - left.Start.y;
    const float rightDx = right.End.x - right.Start.x;
    const float rightDy = right.End.y - right.Start.y;
    const float denominator = leftDx * rightDy - leftDy * rightDx;
    if (std::abs(denominator) <= kCompoundPolygonEpsilon)
    {
        return false;
    }

    const float offsetX = right.Start.x - left.Start.x;
    const float offsetY = right.Start.y - left.Start.y;
    const float leftT = (offsetX * rightDy - offsetY * rightDx) / denominator;
    const float rightT = (offsetX * leftDy - offsetY * leftDx) / denominator;

    // endpoint交差は既に頂点Yがscan boundaryへ入るため、区間内部の交差だけを追加します。
    if (leftT <= kCompoundPolygonEpsilon ||
        leftT >= 1.0f - kCompoundPolygonEpsilon ||
        rightT <= kCompoundPolygonEpsilon ||
        rightT >= 1.0f - kCompoundPolygonEpsilon)
    {
        return false;
    }

    outY = left.Start.y + leftDy * leftT;
    return true;
}

void AppendUniqueScanY(std::vector<float>& values, float value)
{
    for (float existing : values)
    {
        if (std::abs(existing - value) <= kCompoundPolygonEpsilon)
        {
            return;
        }
    }
    values.push_back(value);
}

bool IsFillStateActive(int value, UIFillRule fillRule)
{
    if (fillRule == UIFillRule::EvenOdd)
    {
        return (value & 1) != 0;
    }
    return value != 0;
}

void AppendTriangleIfValid(
    UIDrawList& drawList,
    const math::Vec2& a,
    const math::Vec2& b,
    const math::Vec2& c,
    const math::Vec4& color)
{
    if (std::abs(Cross2D(a, b, c)) <= kCompoundPolygonEpsilon)
    {
        return;
    }
    drawList.AddPolygon(std::vector<math::Vec2>{ a, b, c }, color);
}

void TessellateCompoundPolygon(
    UIDrawList& drawList,
    const std::vector<std::vector<math::Vec2>>& contours,
    UIFillRule fillRule,
    const math::Vec4& color)
{
    std::vector<CompoundScanEdge> edges;
    std::vector<float> scanYValues;

    for (const std::vector<math::Vec2>& sourceContour : contours)
    {
        const std::vector<math::Vec2> contour = NormalizeContour(sourceContour);
        if (contour.size() < 3u)
        {
            continue;
        }

        for (const math::Vec2& point : contour)
        {
            AppendUniqueScanY(scanYValues, point.y);
        }

        for (std::size_t index = 0u; index < contour.size(); ++index)
        {
            const math::Vec2& start = contour[index];
            const math::Vec2& end = contour[(index + 1u) % contour.size()];
            if (IsSamePoint(start, end) == true ||
                std::abs(end.y - start.y) <= kCompoundPolygonEpsilon)
            {
                // horizontal edgeはscanlineを横切らないためwinding更新には不要です。
                // ただし両endpointのYは上でscan boundaryへ追加済みなので形状境界は失われません。
                continue;
            }

            CompoundScanEdge edge;
            edge.Start = start;
            edge.End = end;
            edge.WindingDelta = end.y > start.y ? 1 : -1;
            edges.push_back(edge);
        }
    }

    if (edges.size() < 2u || scanYValues.size() < 2u)
    {
        return;
    }

    // subpath同士が交差する場合もfill-ruleを正しく評価できるよう、edge交点のYでscan slabを分割します。
    // これにより各slab内部ではactive edgeの左右順序が変化せず、区間ごとのwinding/parityを安定して追跡できます。
    for (std::size_t leftIndex = 0u; leftIndex < edges.size(); ++leftIndex)
    {
        for (std::size_t rightIndex = leftIndex + 1u; rightIndex < edges.size(); ++rightIndex)
        {
            float intersectionY = 0.0f;
            if (TryFindProperIntersectionY(edges[leftIndex], edges[rightIndex], intersectionY) == true)
            {
                AppendUniqueScanY(scanYValues, intersectionY);
            }
        }
    }

    std::sort(scanYValues.begin(), scanYValues.end());

    for (std::size_t slabIndex = 0u; slabIndex + 1u < scanYValues.size(); ++slabIndex)
    {
        const float topY = scanYValues[slabIndex];
        const float bottomY = scanYValues[slabIndex + 1u];
        if (bottomY - topY <= kCompoundPolygonEpsilon)
        {
            continue;
        }

        const float middleY = (topY + bottomY) * 0.5f;
        std::vector<ActiveScanEdge> activeEdges;
        activeEdges.reserve(edges.size());
        for (const CompoundScanEdge& edge : edges)
        {
            const float minY = std::min(edge.Start.y, edge.End.y);
            const float maxY = std::max(edge.Start.y, edge.End.y);
            if (middleY <= minY || middleY >= maxY)
            {
                continue;
            }

            ActiveScanEdge active;
            active.Edge = &edge;
            active.MiddleX = EvaluateEdgeX(edge, middleY);
            activeEdges.push_back(active);
        }

        if (activeEdges.size() < 2u)
        {
            continue;
        }

        std::sort(activeEdges.begin(), activeEdges.end(), [](const ActiveScanEdge& left, const ActiveScanEdge& right)
        {
            if (std::abs(left.MiddleX - right.MiddleX) <= kCompoundPolygonEpsilon)
            {
                return left.Edge->WindingDelta < right.Edge->WindingDelta;
            }
            return left.MiddleX < right.MiddleX;
        });

        int fillValue = 0;
        std::size_t groupBegin = 0u;
        while (groupBegin < activeEdges.size())
        {
            std::size_t groupEnd = groupBegin + 1u;
            while (groupEnd < activeEdges.size() &&
                std::abs(activeEdges[groupEnd].MiddleX - activeEdges[groupBegin].MiddleX) <= kCompoundPolygonEpsilon)
            {
                ++groupEnd;
            }

            if (fillRule == UIFillRule::EvenOdd)
            {
                fillValue ^= static_cast<int>((groupEnd - groupBegin) & 1u);
            }
            else
            {
                for (std::size_t index = groupBegin; index < groupEnd; ++index)
                {
                    fillValue += activeEdges[index].Edge->WindingDelta;
                }
            }

            if (groupEnd >= activeEdges.size())
            {
                break;
            }

            std::size_t nextGroupEnd = groupEnd + 1u;
            while (nextGroupEnd < activeEdges.size() &&
                std::abs(activeEdges[nextGroupEnd].MiddleX - activeEdges[groupEnd].MiddleX) <= kCompoundPolygonEpsilon)
            {
                ++nextGroupEnd;
            }

            if (IsFillStateActive(fillValue, fillRule) == true)
            {
                const CompoundScanEdge& leftEdge = *activeEdges[groupBegin].Edge;
                const CompoundScanEdge& rightEdge = *activeEdges[groupEnd].Edge;
                const math::Vec2 topLeft(EvaluateEdgeX(leftEdge, topY), topY);
                const math::Vec2 topRight(EvaluateEdgeX(rightEdge, topY), topY);
                const math::Vec2 bottomRight(EvaluateEdgeX(rightEdge, bottomY), bottomY);
                const math::Vec2 bottomLeft(EvaluateEdgeX(leftEdge, bottomY), bottomY);

                // 1 scan slab内のfilled intervalは左右2本の線形edgeで囲まれる台形です。
                // 2 triangleへ分解して既存SolidPolygon commandへ落とすため、OpenGL backendへSVG固有概念を追加しません。
                AppendTriangleIfValid(drawList, topLeft, topRight, bottomRight, color);
                AppendTriangleIfValid(drawList, bottomRight, bottomLeft, topLeft, color);
            }

            groupBegin = groupEnd;
            static_cast<void>(nextGroupEnd);
        }
    }
}

} // namespace

UIClipRect UIClipRect::Disabled()
{
    return UIClipRect{};
}

UIClipRect UIClipRect::FromRect(const UIRect& rect)
{
    UIClipRect result;
    result.Rect = rect;
    result.Enabled = true;
    return result;
}

UIClipRect UIClipRect::Intersect(const UIClipRect& inheritedClip, const UIRect& rect)
{
    if (inheritedClip.Enabled == false)
    {
        return FromRect(rect);
    }

    UIClipRect result;
    result.Enabled = true;
    result.Rect.Min.x = std::max(inheritedClip.Rect.Min.x, rect.Min.x);
    result.Rect.Min.y = std::max(inheritedClip.Rect.Min.y, rect.Min.y);
    result.Rect.Max.x = std::min(inheritedClip.Rect.Max.x, rect.Max.x);
    result.Rect.Max.y = std::min(inheritedClip.Rect.Max.y, rect.Max.y);

    if (result.Rect.Max.x < result.Rect.Min.x)
    {
        result.Rect.Max.x = result.Rect.Min.x;
    }
    if (result.Rect.Max.y < result.Rect.Min.y)
    {
        result.Rect.Max.y = result.Rect.Min.y;
    }
    return result;
}

bool UIClipRect::Contains(const math::Vec2& point) const
{
    if (Enabled == false)
    {
        return true;
    }

    return point.x >= Rect.Min.x &&
        point.y >= Rect.Min.y &&
        point.x < Rect.Max.x &&
        point.y < Rect.Max.y;
}

UITransform2D UITransform2D::Identity()
{
    return UITransform2D{};
}

UITransform2D UITransform2D::CreateScaleRotation(
    const math::Vec2& pivot,
    float rotation,
    const math::Vec2& scale)
{
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);

    UITransform2D result;
    result.M00 = cosine * scale.x;
    result.M01 = -sine * scale.y;
    result.M10 = sine * scale.x;
    result.M11 = cosine * scale.y;
    result.Translation.x = pivot.x - (result.M00 * pivot.x + result.M01 * pivot.y);
    result.Translation.y = pivot.y - (result.M10 * pivot.x + result.M11 * pivot.y);
    return result;
}

UITransform2D UITransform2D::Combine(
    const UITransform2D& parent,
    const UITransform2D& local)
{
    UITransform2D result;
    result.M00 = parent.M00 * local.M00 + parent.M01 * local.M10;
    result.M01 = parent.M00 * local.M01 + parent.M01 * local.M11;
    result.M10 = parent.M10 * local.M00 + parent.M11 * local.M10;
    result.M11 = parent.M10 * local.M01 + parent.M11 * local.M11;
    result.Translation.x =
        parent.M00 * local.Translation.x +
        parent.M01 * local.Translation.y +
        parent.Translation.x;
    result.Translation.y =
        parent.M10 * local.Translation.x +
        parent.M11 * local.Translation.y +
        parent.Translation.y;
    return result;
}

math::Vec2 UITransform2D::TransformPoint(const math::Vec2& point) const
{
    return math::Vec2(
        M00 * point.x + M01 * point.y + Translation.x,
        M10 * point.x + M11 * point.y + Translation.y);
}

UIRect UITransform2D::TransformRectBounds(const UIRect& rect) const
{
    const math::Vec2 p0 = TransformPoint(rect.Min);
    const math::Vec2 p1 = TransformPoint(math::Vec2(rect.Max.x, rect.Min.y));
    const math::Vec2 p2 = TransformPoint(rect.Max);
    const math::Vec2 p3 = TransformPoint(math::Vec2(rect.Min.x, rect.Max.y));

    UIRect result;
    result.Min.x = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
    result.Min.y = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
    result.Max.x = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    result.Max.y = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
    return result;
}

bool UITransform2D::TryInverseTransformPoint(
    const math::Vec2& point,
    math::Vec2& outPoint) const
{
    const float determinant = M00 * M11 - M01 * M10;
    if (determinant == 0.0f)
    {
        return false;
    }

    const float translatedX = point.x - Translation.x;
    const float translatedY = point.y - Translation.y;
    const float inverseDeterminant = 1.0f / determinant;
    outPoint.x = (M11 * translatedX - M01 * translatedY) * inverseDeterminant;
    outPoint.y = (-M10 * translatedX + M00 * translatedY) * inverseDeterminant;
    return true;
}

void UIDrawList::Clear()
{
    m_Commands.clear();
}

void UIDrawList::AddRect(
    const math::Vec2& min,
    const math::Vec2& max,
    const math::Vec4& color)
{
    if (max.x <= min.x || max.y <= min.y)
    {
        return;
    }

    UIDrawCommand command;
    command.Type = UIDrawCommandType::SolidRect;
    command.Rect.Min = min;
    command.Rect.Max = max;
    command.Color = color;
    m_Commands.push_back(command);
}

void UIDrawList::AddCircle(
    const math::Vec2& min,
    const math::Vec2& max,
    const math::Vec4& color)
{
    if (max.x <= min.x || max.y <= min.y)
    {
        return;
    }

    UIDrawCommand command;
    command.Type = UIDrawCommandType::SolidCircle;
    command.Rect.Min = min;
    command.Rect.Max = max;
    command.Color = color;
    m_Commands.push_back(command);
}

void UIDrawList::AddPolygon(
    const std::vector<math::Vec2>& points,
    const math::Vec4& color)
{
    if (points.size() < 3u)
    {
        return;
    }

    UIDrawCommand command;
    command.Type = UIDrawCommandType::SolidPolygon;
    command.Color = color;
    command.Points = points;
    command.Rect.Min = points[0u];
    command.Rect.Max = points[0u];
    for (const math::Vec2& point : points)
    {
        command.Rect.Min.x = std::min(command.Rect.Min.x, point.x);
        command.Rect.Min.y = std::min(command.Rect.Min.y, point.y);
        command.Rect.Max.x = std::max(command.Rect.Max.x, point.x);
        command.Rect.Max.y = std::max(command.Rect.Max.y, point.y);
    }

    m_Commands.push_back(std::move(command));
}

void UIDrawList::AddCompoundPolygon(
    const std::vector<std::vector<math::Vec2>>& contours,
    UIFillRule fillRule,
    const math::Vec4& color)
{
    TessellateCompoundPolygon(*this, contours, fillRule, color);
}

void UIDrawList::AddImage(
    const math::Vec2& min,
    const math::Vec2& max,
    const Ref<TextureAsset>& texture,
    const math::Vec4& tintColor,
    const math::Vec2& uvMin,
    const math::Vec2& uvMax)
{
    if (max.x <= min.x || max.y <= min.y)
    {
        return;
    }
    if (texture == nullptr || texture->IsValid() == false)
    {
        return;
    }

    UIDrawCommand command;
    command.Type = UIDrawCommandType::Image;
    command.Rect.Min = min;
    command.Rect.Max = max;
    command.Color = tintColor;
    command.UVMin = uvMin;
    command.UVMax = uvMax;
    command.Texture = texture;
    m_Commands.push_back(command);
}

void UIDrawList::ApplyTransform(std::size_t firstCommand, const UITransform2D& transform)
{
    if (firstCommand >= m_Commands.size())
    {
        return;
    }

    for (std::size_t index = firstCommand; index < m_Commands.size(); ++index)
    {
        m_Commands[index].Transform = transform;
    }
}

void UIDrawList::ApplyClip(std::size_t firstCommand, const UIClipRect& clip)
{
    if (firstCommand >= m_Commands.size())
    {
        return;
    }

    for (std::size_t index = firstCommand; index < m_Commands.size(); ++index)
    {
        m_Commands[index].Clip = clip;
    }
}

const std::vector<UIDrawCommand>& UIDrawList::GetCommands() const
{
    return m_Commands;
}

std::size_t UIDrawList::GetCommandCount() const
{
    return m_Commands.size();
}

bool UIDrawList::IsEmpty() const
{
    return m_Commands.empty();
}

} // namespace Raven
