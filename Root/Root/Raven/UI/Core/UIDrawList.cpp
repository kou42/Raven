#include "Raven/UI/Core/UIDrawList.h"

#include "Raven/Assets/TextureAsset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Raven
{

namespace
{
constexpr float kCompoundPolygonEpsilon = 0.00001f;
constexpr float kCompoundPolygonEpsilonSquared =
    kCompoundPolygonEpsilon * kCompoundPolygonEpsilon;
constexpr std::size_t kInvalidContourIndex = static_cast<std::size_t>(-1);

enum class CompoundBoundaryKind
{
    None,
    Outer,
    Hole
};

struct CompoundContourInfo
{
    std::vector<math::Vec2> Points;
    float SignedArea = 0.0f;
    std::size_t Parent = kInvalidContourIndex;
    int InsideValue = 0;
    bool OutsideFilled = false;
    bool InsideFilled = false;
    CompoundBoundaryKind BoundaryKind = CompoundBoundaryKind::None;
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

float CalculateSignedArea(const std::vector<math::Vec2>& points)
{
    float doubledArea = 0.0f;
    for (std::size_t index = 0u; index < points.size(); ++index)
    {
        const math::Vec2& current = points[index];
        const math::Vec2& next = points[(index + 1u) % points.size()];
        doubledArea += current.x * next.y - next.x * current.y;
    }
    return doubledArea * 0.5f;
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

bool IsPointOnSegment(
    const math::Vec2& point,
    const math::Vec2& start,
    const math::Vec2& end)
{
    if (std::abs(Cross2D(start, end, point)) > kCompoundPolygonEpsilon)
    {
        return false;
    }

    const float minX = std::min(start.x, end.x) - kCompoundPolygonEpsilon;
    const float maxX = std::max(start.x, end.x) + kCompoundPolygonEpsilon;
    const float minY = std::min(start.y, end.y) - kCompoundPolygonEpsilon;
    const float maxY = std::max(start.y, end.y) + kCompoundPolygonEpsilon;
    return point.x >= minX && point.x <= maxX &&
        point.y >= minY && point.y <= maxY;
}

bool IsPointInsidePolygon(
    const math::Vec2& point,
    const std::vector<math::Vec2>& polygon)
{
    bool inside = false;
    for (std::size_t index = 0u, previous = polygon.size() - 1u;
        index < polygon.size();
        previous = index++)
    {
        const math::Vec2& a = polygon[previous];
        const math::Vec2& b = polygon[index];
        if (IsPointOnSegment(point, a, b) == true)
        {
            return true;
        }

        const bool crossesY = (a.y > point.y) != (b.y > point.y);
        if (crossesY == true)
        {
            const float intersectionX =
                (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
            if (point.x < intersectionX)
            {
                inside = inside == false;
            }
        }
    }
    return inside;
}

bool SegmentsIntersect(
    const math::Vec2& a0,
    const math::Vec2& a1,
    const math::Vec2& b0,
    const math::Vec2& b1)
{
    const float c0 = Cross2D(a0, a1, b0);
    const float c1 = Cross2D(a0, a1, b1);
    const float c2 = Cross2D(b0, b1, a0);
    const float c3 = Cross2D(b0, b1, a1);

    if (((c0 > kCompoundPolygonEpsilon && c1 < -kCompoundPolygonEpsilon) ||
         (c0 < -kCompoundPolygonEpsilon && c1 > kCompoundPolygonEpsilon)) &&
        ((c2 > kCompoundPolygonEpsilon && c3 < -kCompoundPolygonEpsilon) ||
         (c2 < -kCompoundPolygonEpsilon && c3 > kCompoundPolygonEpsilon)))
    {
        return true;
    }

    if (std::abs(c0) <= kCompoundPolygonEpsilon && IsPointOnSegment(b0, a0, a1) == true)
    {
        return true;
    }
    if (std::abs(c1) <= kCompoundPolygonEpsilon && IsPointOnSegment(b1, a0, a1) == true)
    {
        return true;
    }
    if (std::abs(c2) <= kCompoundPolygonEpsilon && IsPointOnSegment(a0, b0, b1) == true)
    {
        return true;
    }
    if (std::abs(c3) <= kCompoundPolygonEpsilon && IsPointOnSegment(a1, b0, b1) == true)
    {
        return true;
    }
    return false;
}

bool BridgeIntersectsContour(
    const math::Vec2& bridgeStart,
    const math::Vec2& bridgeEnd,
    const std::vector<math::Vec2>& contour,
    const math::Vec2* allowedEndpoint)
{
    for (std::size_t index = 0u; index < contour.size(); ++index)
    {
        const math::Vec2& edgeStart = contour[index];
        const math::Vec2& edgeEnd = contour[(index + 1u) % contour.size()];
        if (allowedEndpoint != nullptr &&
            (IsSamePoint(edgeStart, *allowedEndpoint) == true ||
             IsSamePoint(edgeEnd, *allowedEndpoint) == true))
        {
            continue;
        }

        if (SegmentsIntersect(bridgeStart, bridgeEnd, edgeStart, edgeEnd) == true)
        {
            return true;
        }
    }
    return false;
}

bool FindVisibleBridge(
    const std::vector<math::Vec2>& outer,
    const std::vector<math::Vec2>& hole,
    const std::vector<std::vector<math::Vec2>>& allHoles,
    std::size_t& outOuterIndex,
    std::size_t& outHoleIndex)
{
    float bestDistance = std::numeric_limits<float>::max();
    bool found = false;

    for (std::size_t outerIndex = 0u; outerIndex < outer.size(); ++outerIndex)
    {
        for (std::size_t holeIndex = 0u; holeIndex < hole.size(); ++holeIndex)
        {
            const math::Vec2& outerPoint = outer[outerIndex];
            const math::Vec2& holePoint = hole[holeIndex];
            const float distance = DistanceSquared(outerPoint, holePoint);
            if (distance >= bestDistance)
            {
                continue;
            }

            if (BridgeIntersectsContour(
                    outerPoint,
                    holePoint,
                    outer,
                    &outerPoint) == true ||
                BridgeIntersectsContour(
                    outerPoint,
                    holePoint,
                    hole,
                    &holePoint) == true)
            {
                continue;
            }

            bool intersectsOtherHole = false;
            for (const std::vector<math::Vec2>& otherHole : allHoles)
            {
                if (&otherHole == &hole)
                {
                    continue;
                }
                if (BridgeIntersectsContour(
                        outerPoint,
                        holePoint,
                        otherHole,
                        nullptr) == true)
                {
                    intersectsOtherHole = true;
                    break;
                }
            }
            if (intersectsOtherHole == true)
            {
                continue;
            }

            const math::Vec2 midpoint(
                (outerPoint.x + holePoint.x) * 0.5f,
                (outerPoint.y + holePoint.y) * 0.5f);
            if (IsPointInsidePolygon(midpoint, outer) == false)
            {
                continue;
            }

            bool midpointInsideHole = false;
            for (const std::vector<math::Vec2>& candidateHole : allHoles)
            {
                if (IsPointInsidePolygon(midpoint, candidateHole) == true)
                {
                    midpointInsideHole = true;
                    break;
                }
            }
            if (midpointInsideHole == true)
            {
                continue;
            }

            bestDistance = distance;
            outOuterIndex = outerIndex;
            outHoleIndex = holeIndex;
            found = true;
        }
    }
    return found;
}

std::vector<math::Vec2> MergeHoleIntoOuter(
    const std::vector<math::Vec2>& outer,
    const std::vector<math::Vec2>& hole,
    std::size_t outerIndex,
    std::size_t holeIndex)
{
    std::vector<math::Vec2> merged;
    merged.reserve(outer.size() + hole.size() + 2u);

    for (std::size_t index = 0u; index <= outerIndex; ++index)
    {
        merged.push_back(outer[index]);
    }

    // Holeを一周して同じbridgeを戻ることで、穴領域を切り開いたweakly-simple polygonへ変換します。
    // bridge端点の重複は意図的で、後段Ear Clippingでは同座標点をTriangle内部判定から除外します。
    for (std::size_t step = 0u; step < hole.size(); ++step)
    {
        merged.push_back(hole[(holeIndex + step) % hole.size()]);
    }
    merged.push_back(hole[holeIndex]);
    merged.push_back(outer[outerIndex]);

    for (std::size_t index = outerIndex + 1u; index < outer.size(); ++index)
    {
        merged.push_back(outer[index]);
    }
    return merged;
}

bool IsPointStrictlyInsideTriangle(
    const math::Vec2& point,
    const math::Vec2& a,
    const math::Vec2& b,
    const math::Vec2& c,
    float windingSign)
{
    if (IsSamePoint(point, a) == true ||
        IsSamePoint(point, b) == true ||
        IsSamePoint(point, c) == true)
    {
        return false;
    }

    return Cross2D(a, b, point) * windingSign > kCompoundPolygonEpsilon &&
        Cross2D(b, c, point) * windingSign > kCompoundPolygonEpsilon &&
        Cross2D(c, a, point) * windingSign > kCompoundPolygonEpsilon;
}

bool TriangulateWeakPolygon(
    const std::vector<math::Vec2>& polygon,
    std::vector<std::array<math::Vec2, 3u>>& outTriangles)
{
    outTriangles.clear();
    if (polygon.size() < 3u)
    {
        return false;
    }

    const float signedArea = CalculateSignedArea(polygon);
    if (std::abs(signedArea) <= kCompoundPolygonEpsilon)
    {
        return false;
    }
    const float windingSign = signedArea > 0.0f ? 1.0f : -1.0f;

    std::vector<std::size_t> remaining;
    remaining.reserve(polygon.size());
    for (std::size_t index = 0u; index < polygon.size(); ++index)
    {
        remaining.push_back(index);
    }

    while (remaining.size() > 3u)
    {
        bool earFound = false;
        for (std::size_t currentIndex = 0u; currentIndex < remaining.size(); ++currentIndex)
        {
            const std::size_t previousIndex =
                (currentIndex + remaining.size() - 1u) % remaining.size();
            const std::size_t nextIndex = (currentIndex + 1u) % remaining.size();
            const std::size_t previousVertex = remaining[previousIndex];
            const std::size_t currentVertex = remaining[currentIndex];
            const std::size_t nextVertex = remaining[nextIndex];

            const math::Vec2& a = polygon[previousVertex];
            const math::Vec2& b = polygon[currentVertex];
            const math::Vec2& c = polygon[nextVertex];
            if (Cross2D(a, b, c) * windingSign <= kCompoundPolygonEpsilon)
            {
                continue;
            }

            bool containsVertex = false;
            for (std::size_t candidateVertex : remaining)
            {
                if (candidateVertex == previousVertex ||
                    candidateVertex == currentVertex ||
                    candidateVertex == nextVertex)
                {
                    continue;
                }
                if (IsPointStrictlyInsideTriangle(
                        polygon[candidateVertex],
                        a,
                        b,
                        c,
                        windingSign) == true)
                {
                    containsVertex = true;
                    break;
                }
            }
            if (containsVertex == true)
            {
                continue;
            }

            outTriangles.push_back({ a, b, c });
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(currentIndex));
            earFound = true;
            break;
        }

        if (earFound == false)
        {
            // bridge端点やtessellation由来の一直線頂点が残った場合は、面積を持たないcornerだけ除去して再試行します。
            bool removedDegenerate = false;
            for (std::size_t currentIndex = 0u; currentIndex < remaining.size(); ++currentIndex)
            {
                const std::size_t previousIndex =
                    (currentIndex + remaining.size() - 1u) % remaining.size();
                const std::size_t nextIndex = (currentIndex + 1u) % remaining.size();
                const math::Vec2& a = polygon[remaining[previousIndex]];
                const math::Vec2& b = polygon[remaining[currentIndex]];
                const math::Vec2& c = polygon[remaining[nextIndex]];
                if (IsSamePoint(a, b) == true ||
                    IsSamePoint(b, c) == true ||
                    std::abs(Cross2D(a, b, c)) <= kCompoundPolygonEpsilon)
                {
                    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(currentIndex));
                    removedDegenerate = true;
                    break;
                }
            }

            if (removedDegenerate == false || remaining.size() < 3u)
            {
                outTriangles.clear();
                return false;
            }
        }
    }

    const math::Vec2& a = polygon[remaining[0u]];
    const math::Vec2& b = polygon[remaining[1u]];
    const math::Vec2& c = polygon[remaining[2u]];
    if (std::abs(Cross2D(a, b, c)) > kCompoundPolygonEpsilon)
    {
        outTriangles.push_back({ a, b, c });
    }
    return outTriangles.empty() == false;
}

bool ResolveCompoundContours(
    const std::vector<std::vector<math::Vec2>>& contours,
    UIFillRule fillRule,
    std::vector<CompoundContourInfo>& outInfos)
{
    outInfos.clear();
    for (const std::vector<math::Vec2>& source : contours)
    {
        CompoundContourInfo info;
        info.Points = NormalizeContour(source);
        if (info.Points.size() < 3u)
        {
            continue;
        }
        info.SignedArea = CalculateSignedArea(info.Points);
        if (std::abs(info.SignedArea) <= kCompoundPolygonEpsilon)
        {
            continue;
        }
        outInfos.push_back(std::move(info));
    }

    if (outInfos.empty() == true)
    {
        return false;
    }

    // 各輪郭の直接包含親を求めます。最小面積の包含輪郭を選ぶことで、輪郭の入れ子階層を構築できます。
    for (std::size_t childIndex = 0u; childIndex < outInfos.size(); ++childIndex)
    {
        float bestParentArea = std::numeric_limits<float>::max();
        const float childArea = std::abs(outInfos[childIndex].SignedArea);
        const math::Vec2 sample = outInfos[childIndex].Points[0u];
        for (std::size_t parentIndex = 0u; parentIndex < outInfos.size(); ++parentIndex)
        {
            if (parentIndex == childIndex)
            {
                continue;
            }
            const float parentArea = std::abs(outInfos[parentIndex].SignedArea);
            if (parentArea <= childArea || parentArea >= bestParentArea)
            {
                continue;
            }
            if (IsPointInsidePolygon(sample, outInfos[parentIndex].Points) == true)
            {
                outInfos[childIndex].Parent = parentIndex;
                bestParentArea = parentArea;
            }
        }
    }

    std::vector<std::size_t> order(outInfos.size());
    for (std::size_t index = 0u; index < order.size(); ++index)
    {
        order[index] = index;
    }
    std::sort(order.begin(), order.end(), [&outInfos](std::size_t left, std::size_t right)
    {
        return std::abs(outInfos[left].SignedArea) > std::abs(outInfos[right].SignedArea);
    });

    for (std::size_t index : order)
    {
        CompoundContourInfo& info = outInfos[index];
        const int outsideValue = info.Parent == kInvalidContourIndex
            ? 0
            : outInfos[info.Parent].InsideValue;

        int insideValue = outsideValue;
        if (fillRule == UIFillRule::EvenOdd)
        {
            insideValue = outsideValue == 0 ? 1 : 0;
            info.OutsideFilled = outsideValue != 0;
            info.InsideFilled = insideValue != 0;
        }
        else
        {
            insideValue += info.SignedArea > 0.0f ? 1 : -1;
            info.OutsideFilled = outsideValue != 0;
            info.InsideFilled = insideValue != 0;
        }
        info.InsideValue = insideValue;

        if (info.OutsideFilled == false && info.InsideFilled == true)
        {
            info.BoundaryKind = CompoundBoundaryKind::Outer;
        }
        else if (info.OutsideFilled == true && info.InsideFilled == false)
        {
            info.BoundaryKind = CompoundBoundaryKind::Hole;
        }
    }
    return true;
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
    std::vector<CompoundContourInfo> infos;
    if (ResolveCompoundContours(contours, fillRule, infos) == false)
    {
        return;
    }

    for (std::size_t outerIndex = 0u; outerIndex < infos.size(); ++outerIndex)
    {
        if (infos[outerIndex].BoundaryKind != CompoundBoundaryKind::Outer)
        {
            continue;
        }

        std::vector<math::Vec2> outer = infos[outerIndex].Points;
        if (CalculateSignedArea(outer) < 0.0f)
        {
            std::reverse(outer.begin(), outer.end());
        }

        std::vector<std::vector<math::Vec2>> holes;
        for (std::size_t holeIndex = 0u; holeIndex < infos.size(); ++holeIndex)
        {
            if (infos[holeIndex].BoundaryKind != CompoundBoundaryKind::Hole)
            {
                continue;
            }

            std::size_t ancestor = infos[holeIndex].Parent;
            while (ancestor != kInvalidContourIndex &&
                infos[ancestor].BoundaryKind == CompoundBoundaryKind::None)
            {
                ancestor = infos[ancestor].Parent;
            }
            if (ancestor != outerIndex)
            {
                continue;
            }

            std::vector<math::Vec2> hole = infos[holeIndex].Points;
            if (CalculateSignedArea(hole) > 0.0f)
            {
                std::reverse(hole.begin(), hole.end());
            }
            holes.push_back(std::move(hole));
        }

        std::vector<math::Vec2> merged = outer;
        for (const std::vector<math::Vec2>& hole : holes)
        {
            std::size_t bridgeOuter = 0u;
            std::size_t bridgeHole = 0u;
            if (FindVisibleBridge(merged, hole, holes, bridgeOuter, bridgeHole) == false)
            {
                // 接触輪郭など可視bridgeを安全に決められない入力は、誤ったfillを生成せずこのregionだけ描画しません。
                merged.clear();
                break;
            }
            merged = MergeHoleIntoOuter(merged, hole, bridgeOuter, bridgeHole);
        }

        if (merged.size() < 3u)
        {
            continue;
        }

        std::vector<std::array<math::Vec2, 3u>> triangles;
        if (TriangulateWeakPolygon(merged, triangles) == false)
        {
            continue;
        }

        for (const std::array<math::Vec2, 3u>& triangle : triangles)
        {
            AddPolygon(
                std::vector<math::Vec2>{ triangle[0u], triangle[1u], triangle[2u] },
                color);
        }
    }
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
