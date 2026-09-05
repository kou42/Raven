#include "Raven/UI/Widgets/UIPolygon.h"

#include <utility>

namespace Raven
{

void UIPolygon::SetPoints(std::vector<math::Vec2> points)
{
    // SVG path/polygonでは閉輪郭を明示するため、先頭頂点を末尾へ再記述する入力があります。
    // Ear Clippingでは同一点の重複が0面積cornerを作るため、Polygon表現として不要な閉端点だけ正規化します。
    if (points.size() >= 2u)
    {
        const float deltaX = points.front().x - points.back().x;
        const float deltaY = points.front().y - points.back().y;
        constexpr float kDuplicatePointEpsilonSquared = 0.000001f;
        if (deltaX * deltaX + deltaY * deltaY <= kDuplicatePointEpsilonSquared)
        {
            points.pop_back();
        }
    }

    m_Points = std::move(points);
}

const std::vector<math::Vec2>& UIPolygon::GetPoints() const
{
    return m_Points;
}

void UIPolygon::SetFillColor(const math::Vec4& color)
{
    m_FillColor = color;
}

const math::Vec4& UIPolygon::GetFillColor() const
{
    return m_FillColor;
}

void UIPolygon::OnBuildDrawList(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    if (m_Points.size() < 3u)
    {
        return;
    }

    std::vector<math::Vec2> absolutePoints;
    absolutePoints.reserve(m_Points.size());
    for (const math::Vec2& point : m_Points)
    {
        absolutePoints.push_back(math::Vec2(
            absolutePosition.x + point.x,
            absolutePosition.y + point.y));
    }

    drawList.AddPolygon(absolutePoints, ApplyVisualColor(m_FillColor));
}

} // namespace Raven
