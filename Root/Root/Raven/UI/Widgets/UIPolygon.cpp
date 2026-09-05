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
    m_ContourClosed.clear();
    if (points.empty() == false)
    {
        m_Contours.push_back(std::move(points));
        m_ContourClosed.push_back(true);
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

void UIPolygon::SetContourClosed(std::vector<bool> closed)
{
    m_ContourClosed = std::move(closed);
}

const std::vector<bool>& UIPolygon::GetContourClosed() const
{
    return m_ContourClosed;
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

void UIPolygon::SetStrokeColor(const math::Vec4& color)
{
    m_StrokeColor = color;
}

const math::Vec4& UIPolygon::GetStrokeColor() const
{
    return m_StrokeColor;
}

void UIPolygon::SetStrokeWidth(float width)
{
    m_StrokeWidth = width;
}

float UIPolygon::GetStrokeWidth() const
{
    return m_StrokeWidth;
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
        if (contour.size() < 2u)
        {
            absoluteContours.emplace_back();
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

    // SVG fillは閉じた輪郭だけを対象にします。open subpathはstrokeには残しますが、
    // fill-ruleの輪郭集合へ混ぜないことで暗黙のcloseを描画geometryへ誤適用しません。
    if (m_FillColor.w > 0.0f)
    {
        std::vector<std::vector<math::Vec2>> fillContours;
        for (std::size_t index = 0u; index < absoluteContours.size(); ++index)
        {
            const bool closed = index >= m_ContourClosed.size() || m_ContourClosed[index] == true;
            if (closed == true && absoluteContours[index].size() >= 3u)
            {
                fillContours.push_back(absoluteContours[index]);
            }
        }

        if (fillContours.empty() == false)
        {
            const math::Vec4 fillColor = ApplyVisualColor(m_FillColor);
            if (m_UseCompoundFill == true)
            {
                drawList.AddCompoundPolygon(fillContours, m_FillRule, fillColor);
            }
            else
            {
                drawList.AddPolygon(fillContours[0u], fillColor);
            }
        }
    }

    if (m_StrokeColor.w <= 0.0f || m_StrokeWidth <= 0.0f)
    {
        return;
    }

    const math::Vec4 strokeColor = ApplyVisualColor(m_StrokeColor);
    for (std::size_t index = 0u; index < absoluteContours.size(); ++index)
    {
        if (absoluteContours[index].size() < 2u)
        {
            continue;
        }
        const bool closed = index >= m_ContourClosed.size() || m_ContourClosed[index] == true;
        drawList.AddPolyline(absoluteContours[index], m_StrokeWidth, strokeColor, closed);
    }
}

} // namespace Raven
