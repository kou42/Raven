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
// Raven UIの汎用塗りつぶしPolygon Widgetです。
//
// 単一輪郭は従来どおりSetPoints()で扱い、複数輪郭はSetContours()でcompound polygonとして保持します。
// fill-ruleの解決や穴付き三角形化はWidgetへ持ち込まずUIDrawListへ委譲します。
class UIPolygon final : public UIElement
{
public:
    void SetPoints(std::vector<math::Vec2> points);
    const std::vector<math::Vec2>& GetPoints() const;

    void SetContours(std::vector<std::vector<math::Vec2>> contours);
    const std::vector<std::vector<math::Vec2>>& GetContours() const;

    void SetFillRule(UIFillRule fillRule);
    UIFillRule GetFillRule() const;

    void SetFillColor(const math::Vec4& color);
    const math::Vec4& GetFillColor() const;

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    std::vector<std::vector<math::Vec2>> m_Contours;
    UIFillRule m_FillRule = UIFillRule::NonZero;
    math::Vec4 m_FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace Raven
