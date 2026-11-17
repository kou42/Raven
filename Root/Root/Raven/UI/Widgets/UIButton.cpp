#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Core/UIContext.h"

#include <utility>

namespace Raven
{

namespace
{
// Context未接続時もInstanceごとに4色を複製しません。不変でプロセス全体より長く参照されません。
const UIButtonStyle DefaultButtonStyle{};
}

struct UIButton::StyleOverrides
{
    UIButtonStyle Style;
    // 個別SetColorはThemeより優先します。未設定の色だけTheme変更へ追従します。
    std::uint8_t Mask = 0u;
};

UIButton::UIButton()
{
    // Mouse専用Widgetにせず、Tab移動後のEnter / Space操作をOnKeyEventへ届けます。
    SetFocusable(true);
}

UIButton::~UIButton() = default;

void UIButton::SetNormalColor(const math::Vec4& color)
{
    constexpr std::uint8_t bit = 1u;
    if (m_Overrides != nullptr && (m_Overrides->Mask & bit) != 0u && m_Overrides->Style.NormalColor == color)
    {
        return;
    }
    if (m_Overrides == nullptr)
    {
        m_Overrides = CreateScope<StyleOverrides>();
    }
    // Themeと同値でも明示指定を記録します。将来のTheme変更で色が上書きされることを防ぎます。
    m_Overrides->Style.NormalColor = color;
    m_Overrides->Mask |= bit;
    InvalidatePaint();
}

void UIButton::SetHoveredColor(const math::Vec4& color)
{
    constexpr std::uint8_t bit = 2u;
    if (m_Overrides != nullptr && (m_Overrides->Mask & bit) != 0u && m_Overrides->Style.HoveredColor == color)
    {
        return;
    }
    if (m_Overrides == nullptr)
    {
        m_Overrides = CreateScope<StyleOverrides>();
    }
    // Themeと同値でも明示指定を記録します。将来のTheme変更で色が上書きされることを防ぎます。
    m_Overrides->Style.HoveredColor = color;
    m_Overrides->Mask |= bit;
    InvalidatePaint();
}

void UIButton::SetPressedColor(const math::Vec4& color)
{
    constexpr std::uint8_t bit = 4u;
    if (m_Overrides != nullptr && (m_Overrides->Mask & bit) != 0u && m_Overrides->Style.PressedColor == color)
    {
        return;
    }
    if (m_Overrides == nullptr)
    {
        m_Overrides = CreateScope<StyleOverrides>();
    }
    // Themeと同値でも明示指定を記録します。将来のTheme変更で色が上書きされることを防ぎます。
    m_Overrides->Style.PressedColor = color;
    m_Overrides->Mask |= bit;
    InvalidatePaint();
}

void UIButton::SetFocusedColor(const math::Vec4& color)
{
    constexpr std::uint8_t bit = 8u;
    if (m_Overrides != nullptr && (m_Overrides->Mask & bit) != 0u && m_Overrides->Style.FocusedColor == color)
    {
        return;
    }
    if (m_Overrides == nullptr)
    {
        m_Overrides = CreateScope<StyleOverrides>();
    }
    // Themeと同値でも明示指定を記録します。将来のTheme変更で色が上書きされることを防ぎます。
    m_Overrides->Style.FocusedColor = color;
    m_Overrides->Mask |= bit;
    InvalidatePaint();
}

void UIButton::SetOnClick(ClickHandler handler)
{
    m_OnClick = std::move(handler);
}

const math::Vec4& UIButton::GetNormalColor() const
{
    if (m_Overrides != nullptr && (m_Overrides->Mask & 1u) != 0u)
    {
        return m_Overrides->Style.NormalColor;
    }
    const UIContext* context = GetContext();
    return context != nullptr ? context->GetTheme().Button.NormalColor : DefaultButtonStyle.NormalColor;
}

const math::Vec4& UIButton::GetHoveredColor() const
{
    if (m_Overrides != nullptr && (m_Overrides->Mask & 2u) != 0u)
    {
        return m_Overrides->Style.HoveredColor;
    }
    const UIContext* context = GetContext();
    return context != nullptr ? context->GetTheme().Button.HoveredColor : DefaultButtonStyle.HoveredColor;
}

const math::Vec4& UIButton::GetPressedColor() const
{
    if (m_Overrides != nullptr && (m_Overrides->Mask & 4u) != 0u)
    {
        return m_Overrides->Style.PressedColor;
    }
    const UIContext* context = GetContext();
    return context != nullptr ? context->GetTheme().Button.PressedColor : DefaultButtonStyle.PressedColor;
}

const math::Vec4& UIButton::GetFocusedColor() const
{
    if (m_Overrides != nullptr && (m_Overrides->Mask & 8u) != 0u)
    {
        return m_Overrides->Style.FocusedColor;
    }
    const UIContext* context = GetContext();
    return context != nullptr ? context->GetTheme().Button.FocusedColor : DefaultButtonStyle.FocusedColor;
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
