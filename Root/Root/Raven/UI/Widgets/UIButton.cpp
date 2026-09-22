#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Core/UIContext.h"

#include <utility>

namespace Raven
{

void UIButton::SetNormalColor(const math::Vec4& color)
{
    m_NormalColor = color;
    m_NormalColorOverride = true;
}

void UIButton::SetHoveredColor(const math::Vec4& color)
{
    m_HoveredColor = color;
    m_HoveredColorOverride = true;
}

void UIButton::SetPressedColor(const math::Vec4& color)
{
    m_PressedColor = color;
    m_PressedColorOverride = true;
}

void UIButton::SetFocusedColor(const math::Vec4& color)
{
    m_FocusedColor = color;
    m_FocusedColorOverride = true;
}

void UIButton::SetOnClick(ClickHandler handler)
{
    m_OnClick = std::move(handler);
}

const math::Vec4& UIButton::GetNormalColor() const
{
    const UIContext* context = GetContext();
    if (m_NormalColorOverride == false && context != nullptr)
    {
        return context->GetTheme().Button.NormalColor;
    }
    return m_NormalColor;
}

const math::Vec4& UIButton::GetHoveredColor() const
{
    const UIContext* context = GetContext();
    if (m_HoveredColorOverride == false && context != nullptr)
    {
        return context->GetTheme().Button.HoveredColor;
    }
    return m_HoveredColor;
}

const math::Vec4& UIButton::GetPressedColor() const
{
    const UIContext* context = GetContext();
    if (m_PressedColorOverride == false && context != nullptr)
    {
        return context->GetTheme().Button.PressedColor;
    }
    return m_PressedColor;
}

const math::Vec4& UIButton::GetFocusedColor() const
{
    const UIContext* context = GetContext();
    if (m_FocusedColorOverride == false && context != nullptr)
    {
        return context->GetTheme().Button.FocusedColor;
    }
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
    const math::Vec4* color = &GetNormalColor();
    if (IsPressed() == true)
    {
        color = &GetPressedColor();
    }
    else if (IsHovered() == true)
    {
        color = &GetHoveredColor();
    }
    else if (IsFocused() == true)
    {
        color = &GetFocusedColor();
    }

    const math::Vec2& size = GetSize();
    drawList.AddRect(
        absolutePosition,
        math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y),
        ApplyVisualColor(*color));
}

} // namespace Raven
