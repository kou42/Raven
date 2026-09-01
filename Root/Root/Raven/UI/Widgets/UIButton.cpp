#include "Raven/UI/Widgets/UIButton.h"

#include <utility>

namespace Raven
{

void UIButton::SetNormalColor(const math::Vec4& color) { m_NormalColor = color; }
void UIButton::SetHoveredColor(const math::Vec4& color) { m_HoveredColor = color; }
void UIButton::SetPressedColor(const math::Vec4& color) { m_PressedColor = color; }
void UIButton::SetOnClick(ClickHandler handler) { m_OnClick = std::move(handler); }
const math::Vec4& UIButton::GetNormalColor() const { return m_NormalColor; }
const math::Vec4& UIButton::GetHoveredColor() const { return m_HoveredColor; }
const math::Vec4& UIButton::GetPressedColor() const { return m_PressedColor; }

void UIButton::OnMouseEvent(UIMouseEvent& event)
{
    if (event.Target != this) return;
    if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
    {
        event.Handled = true;
        return;
    }
    if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left)
    {
        if (event.PressedTarget == this && m_OnClick != nullptr) m_OnClick();
        event.Handled = true;
    }
}

void UIButton::OnBuildDrawList(UIDrawList& drawList, const math::Vec2& absolutePosition) const
{
    const math::Vec4* color = &m_NormalColor;
    if (IsPressed()) color = &m_PressedColor;
    else if (IsHovered()) color = &m_HoveredColor;

    const math::Vec2& size = GetSize();
    drawList.AddRect(
        absolutePosition,
        math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y),
        ApplyVisualColor(*color));
}

} // namespace Raven
