#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

#include <functional>

namespace Raven
{

// ============================================================================
// UIButton
// ============================================================================
// UIElementのHover / Pressed状態をVisual Stateへ変換する最小Button Widgetです。
// Clickは左ButtonでこのElement上から押下を開始し、同じElement上で離した場合だけ成立します。
class UIButton final : public UIElement
{
public:
    using ClickHandler = std::function<void()>;

    void SetNormalColor(const math::Vec4& color);
    void SetHoveredColor(const math::Vec4& color);
    void SetPressedColor(const math::Vec4& color);
    void SetOnClick(ClickHandler handler);

    const math::Vec4& GetNormalColor() const;
    const math::Vec4& GetHoveredColor() const;
    const math::Vec4& GetPressedColor() const;

protected:
    void OnMouseEvent(UIMouseEvent& event) override;
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const override;

private:
    math::Vec4 m_NormalColor{ 0.24f, 0.24f, 0.28f, 1.0f };
    math::Vec4 m_HoveredColor{ 0.32f, 0.32f, 0.38f, 1.0f };
    math::Vec4 m_PressedColor{ 0.16f, 0.16f, 0.20f, 1.0f };
    ClickHandler m_OnClick;
};

} // namespace Raven
