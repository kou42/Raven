#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

#include <vector>

namespace Raven
{

// ============================================================================
// UIPolygon
// ============================================================================
// Raven UIの汎用塗りつぶしPolygon Widgetです。
//
// PointsはElement左上を原点とするLocal座標で保持します。SVG専用型にはせず、
// Editor Overlayや将来のVector Path tessellation結果からも同じDrawList経路を利用できます。
// 三角形化アルゴリズムはWidgetへ持ち込まずRenderer backend側へ閉じ込めます。
class UIPolygon final : public UIElement
{
public:
    void SetPoints(std::vector<math::Vec2> points);
    const std::vector<math::Vec2>& GetPoints() const;

    void SetFillColor(const math::Vec4& color);
    const math::Vec4& GetFillColor() const;

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    std::vector<math::Vec2> m_Points;
    math::Vec4 m_FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace Raven
