#include "Raven/UI/Widgets/UIButton.h"

#include <utility>

namespace Raven
{

void UIButton::SetNormalColor(const math::Vec4& color)
{
    m_NormalColor = color;
}

void UIButton::SetHoveredColor(const math::Vec4& color)
{
    m_HoveredColor = color;
}

void UIButton::SetPressedColor(const math::Vec4& color)
{
    m_PressedColor = color;
}

void UIButton::SetFocusedColor(const math::Vec4& color)
{
    m_FocusedColor = color;
}

void UIButton::SetOnClick(ClickHandler handler)
{
    m_OnClick = std::move(handler);
}

const math::Vec4& UIButton::GetNormalColor() const
{
    return m_NormalColor;
}

const math::Vec4& UIButton::GetHoveredColor() const
{
    return m_HoveredColor;
}

const math::Vec4& UIButton::GetPressedColor() const
{
    return m_PressedColor;
}

const math::Vec4& UIButton::GetFocusedColor() const
{
    return m_FocusedColor;
}

void UIButton::OnMouseEvent(UIMouseEvent& event)
{
    // Bubble中にParent Buttonまで反応しないよう、実際のHit Targetが自分自身の場合だけ扱います。
    if (event.Target != this)
    {
        return;
    }

    if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
    {
        event.Handled = true;
        return;
    }

    if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left)
    {
        // PressedTargetはMouseDown開始時のHit Elementです。
        // Up時にも自分自身がHitしているため、この一致で標準的なButton Clickを成立させます。
        if (event.PressedTarget == this && m_OnClick != nullptr)
        {
            m_OnClick();
        }

        event.Handled = true;
    }
}

void UIButton::OnKeyEvent(UIKeyEvent& event)
{
    if (IsFocused() == false || event.Pressed == false)
    {
        return;
    }

    if (event.Key == UIKey::Enter || event.Key == UIKey::Space)
    {
        // OS Key RepeatでButton Actionが意図せず連打されないよう、1回の物理押下につき1回だけActivateします。
        if (event.Repeat == false && m_OnClick != nullptr)
        {
            m_OnClick();
        }
        event.Handled = true;
    }
}

void UIButton::OnBuildDrawList(
    UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    const math::Vec4* color = &m_NormalColor;
    if (IsPressed() == true)
    {
        color = &m_PressedColor;
    }
    else if (IsHovered() == true)
    {
        color = &m_HoveredColor;
    }
    else if (IsFocused() == true)
    {
        color = &m_FocusedColor;
    }

    const math::Vec2& size = GetSize();
    drawList.AddRect(
        absolutePosition,
        math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y),
        ApplyVisualColor(*color));
}

} // namespace Raven
