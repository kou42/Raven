#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Widgets/UILabel.h"

#include <functional>
#include <string>
#include <utility>

namespace Raven
{

// 状態は外部からSetCheckedで同期し、入力時だけToggle通知を発行するCheckboxです。
// Labelは装飾専用としてHit Testから外し、行全体をMouse/Keyboardで操作できます。
class UICheckbox final : public UIElement
{
public:
    using ToggleHandler = std::function<void(bool)>;

    UICheckbox()
    {
        SetFocusable(true);
        auto label = CreateScope<UILabel>();
        label->SetHitTestVisible(false);
        m_Label = static_cast<UILabel*>(AddChild(std::move(label)));
    }

    void SetChecked(bool checked) { m_Checked = checked; }
    bool IsChecked() const { return m_Checked; }
    void SetOnToggle(ToggleHandler handler) { m_OnToggle = std::move(handler); }

    void SetCaption(const std::string& caption, const Ref<UIFontAtlas>& font)
    {
        if (m_Label == nullptr)
        {
            return;
        }
        m_Label->SetText(caption);
        if (font != nullptr)
        {
            m_Label->SetFont(font);
        }
        m_Label->SetPosition(math::Vec2(28.0f, 4.0f));
        const math::Vec2& size = GetPreferredSize();
        m_Label->SetPreferredSize(math::Vec2(
            std::max(0.0f, size.x - 32.0f), std::max(0.0f, size.y - 8.0f)));
    }

protected:
    void OnMouseEvent(UIMouseEvent& event) override
    {
        if (event.Target != this)
        {
            return;
        }
        if (event.Type == UIMouseEventType::Down && event.Button == UIMouseButton::Left)
        {
            event.Handled = true;
        }
        else if (event.Type == UIMouseEventType::Up && event.Button == UIMouseButton::Left)
        {
            if (event.PressedTarget == this)
            {
                Toggle();
            }
            event.Handled = true;
        }
    }

    void OnKeyEvent(UIKeyEvent& event) override
    {
        if (IsFocused() == false || event.Pressed == false)
        {
            return;
        }
        if (event.Key == UIKey::Space || event.Key == UIKey::Enter)
        {
            if (event.Repeat == false)
            {
                Toggle();
            }
            event.Handled = true;
        }
    }

    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const override
    {
        const math::Vec4 background = IsPressed() == true
            ? math::Vec4(0.18f, 0.24f, 0.36f, 1.0f)
            : (IsHovered() == true || IsFocused() == true
                ? math::Vec4(0.28f, 0.36f, 0.52f, 1.0f)
                : math::Vec4(0.16f, 0.19f, 0.26f, 1.0f));
        drawList.AddRect(position,
            math::Vec2(position.x + GetSize().x, position.y + GetSize().y),
            ApplyVisualColor(background));
        const math::Vec2 boxMin(position.x + 5.0f, position.y + 5.0f);
        const math::Vec2 boxMax(position.x + 21.0f, position.y + 21.0f);
        drawList.AddRect(boxMin, boxMax,
            ApplyVisualColor(math::Vec4(0.85f, 0.87f, 0.92f, 1.0f)));
        if (m_Checked == true)
        {
            drawList.AddRect(math::Vec2(boxMin.x + 4.0f, boxMin.y + 4.0f),
                math::Vec2(boxMax.x - 4.0f, boxMax.y - 4.0f),
                ApplyVisualColor(math::Vec4(0.12f, 0.55f, 0.88f, 1.0f)));
        }
    }

private:
    void Toggle()
    {
        m_Checked = (m_Checked == false);
        if (m_OnToggle != nullptr)
        {
            m_OnToggle(m_Checked);
        }
    }

    bool m_Checked = false;
    UILabel* m_Label = nullptr;
    ToggleHandler m_OnToggle;
};

} // namespace Raven
