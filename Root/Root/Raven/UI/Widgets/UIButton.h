#pragma once

#include "Raven/Math/MathVector.h"
#include "Raven/UI/Core/UIElement.h"

#include <functional>
#include <utility>

namespace Raven
{

// ============================================================================
// UIButton
// ============================================================================
// UIElementのHover / Pressed状態をVisual Stateへ変換する最小Button Widgetです。
// Clickは左ButtonでこのElement上から押下を開始し、同じElement上で離した場合だけ成立します。
// Mouse Captureを明示API化する前段階として、UIContextが保持するPressed状態を利用します。
class UIButton final : public UIElement
{
public:
    using ClickHandler = std::function<void()>;

    void SetNormalColor(const math::Vec4& color) { m_NormalColor = color; }
    void SetHoveredColor(const math::Vec4& color) { m_HoveredColor = color; }
    void SetPressedColor(const math::Vec4& color) { m_PressedColor = color; }
    void SetOnClick(ClickHandler handler) { m_OnClick = std::move(handler); }

    const math::Vec4& GetNormalColor() const { return m_NormalColor; }
    const math::Vec4& GetHoveredColor() const { return m_HoveredColor; }
    const math::Vec4& GetPressedColor() const { return m_PressedColor; }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
        // Bubble中にParent Buttonまで反応しないよう、実際のHit Targetが自分自身の場合だけ扱います。
        if (event.Target != this)
        {
            return;
        }

        if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
        {
            // UIContextはRouting前にPressed状態を更新するため、ここでは状態の所有権を持ちません。
            // DownをButtonが消費することで、背後のWidgetや親Containerの操作との競合を避けます。
            event.Handled = true;
            return;
        }

        if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left)
        {
            // UIContextはMouseUp Routing前にPressedを解除するため、Down開始位置をButton側でも記録します。
            // Click成立条件は「このButtonでDown開始」かつ「このButton上でUp」です。
            if (m_PressStartedHere == true && m_OnClick != nullptr)
            {
                m_OnClick();
            }

            m_PressStartedHere = false;
            event.Handled = true;
            return;
        }

        if (event.Type == UIMouseEventType::Move)
        {
            return;
        }
    }

    void OnBuildDrawList(
        UIDrawList& drawList,
        const math::Vec2& absolutePosition) const override
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

        const math::Vec2& size = GetSize();
        drawList.AddRect(
            absolutePosition,
            math::Vec2(absolutePosition.x + size.x, absolutePosition.y + size.y),
            *color);
    }

private:
    math::Vec4 m_NormalColor{ 0.24f, 0.24f, 0.28f, 1.0f };
    math::Vec4 m_HoveredColor{ 0.32f, 0.32f, 0.38f, 1.0f };
    math::Vec4 m_PressedColor{ 0.16f, 0.16f, 0.20f, 1.0f };
    ClickHandler m_OnClick;
    bool m_PressStartedHere = false;
};

} // namespace Raven
