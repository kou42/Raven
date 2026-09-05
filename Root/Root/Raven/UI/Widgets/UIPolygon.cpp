#include "Raven/UI/Widgets/UIPolygon.h"

#include <utility>

namespace Raven
{

namespace
{

void NormalizeClosedContour(std::vector<math::Vec2>& points)
{
    if (points.size() < 2u)
    {
        return;
    }

    const float deltaX = points.front().x - points.back().x;
    const float deltaY = points.front().y - points.back().y;
    constexpr float kDuplicatePointEpsilonSquared = 0.000001f;
    if (deltaX * deltaX + deltaY * deltaY <= kDuplicatePointEpsilonSquared)
    {
        points.pop_back();
    }
}

} // namespace

void UIPolygon::SetPoints(std::vector<math::Vec2> points)
{
    NormalizeClosedContour(points);
    m_Contours.clear();
    if (points.empty() == false)
    {
        m_Contours.push_back(std::move(points));
    }
    m_UseCompoundFill = false;
}

const std::vector<math::Vec2>& UIPolygon::GetPoints() const
{
    static const std::vector<math::Vec2> empty;
    return m_Contours.empty() == true ? empty : m_Contours[0u];
}

void UIPolygon::SetContours(std::vector<std::vector<math::Vec2>> contours)
{
    for (std::vector<math::Vec2>& contour : contours)
    {
        NormalizeClosedContour(contour);
    }
    m_Contours = std::move(contours);
    m_UseCompoundFill = true;
}

const std::vector<std::vector<math::Vec2>>& UIPolygon::GetContours() const
{
    return m_Contours;
}

void UIPolygon::SetFillRule(UIFillRule fillRule)
{
    m_FillRule = fillRule;
}

UIFillRule UIPolygon::GetFillRule() const
{
    return m_FillRule;
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
    if (m_Contours.empty() == true)
    {
        return;
    }

    std::vector<std::vector<math::Vec2>> absoluteContours;
    absoluteContours.reserve(m_Contours.size());
    for (const std::vector<math::Vec2>& contour : m_Contours)
    {
        if (contour.size() < 3u)
        {
            continue;
        }

        std::vector<math::Vec2> absolutePoints;
        absolutePoints.reserve(contour.size());
        for (const math::Vec2& point : contour)
        {
            absolutePoints.push_back(math::Vec2(
                absolutePosition.x + point.x,
                absolutePosition.y + point.y));
        }
        absoluteContours.push_back(std::move(absolutePoints));
    }

    if (absoluteContours.empty() == true)
    {
        return;
    }

    const math::Vec4 color = ApplyVisualColor(m_FillColor);
    if (m_UseCompoundFill == true)
    {
        // SVG pathは輪郭数1でも自己交差時にfill-ruleの結果が変わるため、常にcompound経路を使います。
        drawList.AddCompoundPolygon(absoluteContours, m_FillRule, color);
        return;
    }

    drawList.AddPolygon(absoluteContours[0u], color);
}

} // namespace Raven
