#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Core/UIElement.h"

#include <vector>

namespace Raven
{

// ============================================================================
// UIPolygon
// ============================================================================
// Raven UIの汎用Path/Polygon Widgetです。
// 単一輪郭は従来どおりSetPoints()で扱い、SVG path等のfill-rule対象はSetContours()でcompound polygonとして保持します。
// fill-ruleとstrokeのgeometry生成はWidgetへ持ち込まずUIDrawListへ委譲します。
class UIPolygon final : public UIElement
{
public:
    void SetPoints(std::vector<math::Vec2> points);
    const std::vector<math::Vec2>& GetPoints() const;

    void SetContours(std::vector<std::vector<math::Vec2>> contours);
    const std::vector<std::vector<math::Vec2>>& GetContours() const;

    // contoursと同じ順序で各輪郭がZ/zで閉じているかを保持します。
    // 指定がない輪郭は従来Polygon互換のためclosedとして扱います。
    void SetContourClosed(std::vector<bool> closed);
    const std::vector<bool>& GetContourClosed() const;

    void SetFillRule(UIFillRule fillRule);
    UIFillRule GetFillRule() const;

    void SetFillColor(const math::Vec4& color);
    const math::Vec4& GetFillColor() const;

    void SetStrokeColor(const math::Vec4& color);
    const math::Vec4& GetStrokeColor() const;

    void SetStrokeWidth(float width);
    float GetStrokeWidth() const;

    void SetStrokeLineCap(UILineCap lineCap);
    UILineCap GetStrokeLineCap() const;

    void SetStrokeLineJoin(UILineJoin lineJoin);
    UILineJoin GetStrokeLineJoin() const;

    void SetStrokeMiterLimit(float miterLimit);
    float GetStrokeMiterLimit() const;

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    std::vector<std::vector<math::Vec2>> m_Contours;
    std::vector<bool> m_ContourClosed;
    UIFillRule m_FillRule = UIFillRule::NonZero;
    bool m_UseCompoundFill = false;
    math::Vec4 m_FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    math::Vec4 m_StrokeColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float m_StrokeWidth = 1.0f;
    UILineCap m_StrokeLineCap = UILineCap::Butt;
    UILineJoin m_StrokeLineJoin = UILineJoin::Miter;
    float m_StrokeMiterLimit = 4.0f;
};

} // namespace Raven
