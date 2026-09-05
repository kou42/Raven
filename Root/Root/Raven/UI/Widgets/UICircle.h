#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

// ============================================================================
// UICircle
// ============================================================================
// Raven UIの汎用円形Widgetです。
//
// SVG専用型にはせず、Retained UI TreeからUIDrawListへ円形描画Commandを生成する責務だけを持ちます。
// これによりSVG <circle>だけでなく、将来のEditor Handle / Badge / Vector UIからも同じ描画経路を再利用できます。
// ElementのSizeは円を内接させる矩形として扱い、非一様ScaleやTransform後は楕円として描画されます。
class UICircle final : public UIElement
{
public:
    void SetFillColor(const math::Vec4& color);
    const math::Vec4& GetFillColor() const;

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    math::Vec4 m_FillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace Raven
