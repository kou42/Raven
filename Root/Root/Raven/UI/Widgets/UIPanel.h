#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

namespace Raven
{

// ============================================================================
// UIPanel
// ============================================================================
// 背景色付き矩形を描画する最小Widgetです。
//
// 現段階ではBorder / Corner Radius / Padding / Layout等は持たず、UIElementが保持する
// Position / SizeとPanel固有のBackgroundColorだけをUIDrawListへ変換します。
// Editor UIとGame UIの双方でContainerの最小単位として利用する想定です。
class UIPanel final : public UIElement
{
public:
    void SetBackgroundColor(const math::Vec4& color);
    const math::Vec4& GetBackgroundColor() const;

protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    math::Vec4 m_BackgroundColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace Raven
