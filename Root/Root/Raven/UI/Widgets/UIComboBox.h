#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Text/UIFontAtlas.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{

// Popup Layerを使う単一選択Widgetです。選択値はindexで管理し、表示文字列とは分離します。
// Popupの所有権はUIContextにあり、ComboBoxはContext所属中だけ非所有Pointerを保持します。
class UIComboBox final : public UIElement
{
public:
    using SelectionChangedHandler = std::function<void(std::size_t, const std::string&)>;
    static constexpr std::size_t NoSelection = std::numeric_limits<std::size_t>::max();

    UIComboBox() { SetFocusable(true); SetSize(math::Vec2(180.0f, 30.0f)); }
    ~UIComboBox() override
    {
        UIContext* context = GetContext();
        if (context != nullptr && m_Popup != nullptr)
        {
            context->RemovePopup(m_Popup);
        }
    }

    void SetFont(const Ref<UIFontAtlas>& font) { m_Font = font; }
    void SetOnSelectionChanged(SelectionChangedHandler handler) { m_OnSelectionChanged = std::move(handler); }
    const std::vector<std::string>& GetOptions() const { return m_Options; }
    std::size_t GetSelectedIndex() const { return m_SelectedIndex; }
    const std::string& GetSelectedText() const
    {
        static const std::string empty;
        return m_SelectedIndex < m_Options.size() ? m_Options[m_SelectedIndex] : empty;
    }

    void SetOptions(std::vector<std::string> options)
    {
        Close();
        DestroyPopup();
        m_Options = std::move(options);
        m_SelectedIndex = NoSelection;
        m_HighlightedIndex = NoSelection;
    }

    bool SetSelectedIndex(std::size_t index)
    {
        if (index >= m_Options.size())
        {
            return false;
        }
        if (index == m_SelectedIndex)
        {
            return true;
        }
        m_SelectedIndex = index;
        if (m_OnSelectionChanged != nullptr)
        {
            m_OnSelectionChanged(index, m_Options[index]);
        }
        return true;
    }

    bool IsOpen() const
    {
        const UIContext* context = GetContext();
        return context != nullptr && m_Popup != nullptr && context->GetOpenPopup() == m_Popup;
    }

    bool Open()
    {
        UIContext* context = GetContext();
        if (context == nullptr || m_Options.empty())
        {
            return false;
        }
        if (IsOpen())
        {
            return true;
        }
        if (m_Popup == nullptr)
        {
            auto popup = CreateScope<UIElement>();
            popup->SetClipChildren(false);
            for (std::size_t index = 0u; index < m_Options.size(); ++index)
            {
                auto row = CreateScope<OptionRow>(*this, index);
                row->SetPosition(math::Vec2(0.0f, static_cast<float>(index) * m_RowHeight));
                row->SetSize(math::Vec2(GetSize().x, m_RowHeight));
                popup->AddChild(std::move(row));
            }
            m_Popup = context->AddPopup(std::move(popup));
            if (m_Popup == nullptr)
            {
                return false;
            }
        }
        const float width = std::max(1.0f, GetSize().x);
        m_Popup->SetSize(math::Vec2(width, static_cast<float>(m_Options.size()) * m_RowHeight));
        for (const auto& child : m_Popup->GetChildren())
        {
            if (child != nullptr)
            {
                child->SetSize(math::Vec2(width, m_RowHeight));
            }
        }
        m_HighlightedIndex = m_SelectedIndex < m_Options.size() ? m_SelectedIndex : 0u;
        context->SetFocus(this);
        return context->OpenPopupAt(m_Popup, this);
    }

    void Close()
    {
        UIContext* context = GetContext();
        if (context != nullptr && IsOpen())
        {
            context->ClosePopup();
        }
    }

protected:
    void OnContextChanged(UIContext* previous, UIContext* current) override
    {
        if (previous != nullptr && previous != current)
        {
            // DetachChildは破棄より先にContextを解除するため、Destructorだけでは
            // Popup Layerに残る選択肢を回収できません。
            DestroyPopup();
        }
    }

    void OnMouseEvent(UIMouseEvent& event) override
    {
        if (event.Target != this || event.Button != UIMouseButton::Left)
        {
            return;
        }
        if (event.Type == UIMouseEventType::Down)
        {
            event.Handled = true;
        }
        else if (event.Type == UIMouseEventType::Up)
        {
            if (event.PressedTarget == this)
            {
                if (IsOpen())
                {
                    Close();
                }
                else
                {
                    Open();
                }
            }
            event.Handled = true;
        }
    }

    void OnKeyEvent(UIKeyEvent& event) override
    {
        if (event.Pressed == false || m_Options.empty())
        {
            return;
        }
        if (event.Key == UIKey::Enter || event.Key == UIKey::Space)
        {
            if (event.Repeat == false)
            {
                if (IsOpen())
                {
                    SelectHighlighted();
                }
                else
                {
                    Open();
                }
            }
            event.Handled = true;
        }
        else if (event.Key == UIKey::Up || event.Key == UIKey::Down)
        {
            if (IsOpen() == false)
            {
                Open();
            }
            else if (event.Key == UIKey::Up)
            {
                m_HighlightedIndex = m_HighlightedIndex == 0u
                    ? m_Options.size() - 1u : m_HighlightedIndex - 1u;
            }
            else
            {
                m_HighlightedIndex = (m_HighlightedIndex + 1u) % m_Options.size();
            }
            event.Handled = true;
        }
        else if (IsOpen() && (event.Key == UIKey::Home || event.Key == UIKey::End))
        {
            m_HighlightedIndex = event.Key == UIKey::Home ? 0u : m_Options.size() - 1u;
            event.Handled = true;
        }
    }

    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const override
    {
        const math::Vec2 size = GetSize();
        const math::Vec4 color = IsPressed() ? math::Vec4(0.16f, 0.16f, 0.20f, 1.0f)
            : IsHovered() || IsOpen() ? math::Vec4(0.32f, 0.32f, 0.38f, 1.0f)
            : math::Vec4(0.24f, 0.24f, 0.28f, 1.0f);
        drawList.AddRect(position, position + size, ApplyVisualColor(color));
        DrawText(drawList, GetSelectedText(), position);
    }

private:
    class OptionRow final : public UIElement
    {
    public:
        OptionRow(UIComboBox& owner, std::size_t index) : m_Owner(owner), m_Index(index) {}
    protected:
        void OnMouseEvent(UIMouseEvent& event) override
        {
            if (event.Target != this || event.Button != UIMouseButton::Left)
            {
                return;
            }
            if (event.Type == UIMouseEventType::Down)
            {
                m_Owner.m_HighlightedIndex = m_Index;
                event.Handled = true;
            }
            else if (event.Type == UIMouseEventType::Up)
            {
                if (event.PressedTarget == this)
                {
                    m_Owner.m_HighlightedIndex = m_Index;
                    m_Owner.SelectHighlighted();
                }
                event.Handled = true;
            }
        }
        void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const override
        {
            const math::Vec2 size = GetSize();
            const bool selected = m_Index == m_Owner.m_SelectedIndex;
            const bool highlighted = m_Index == m_Owner.m_HighlightedIndex;
            const math::Vec4 color = highlighted ? math::Vec4(0.34f, 0.42f, 0.60f, 1.0f)
                : selected ? math::Vec4(0.28f, 0.34f, 0.46f, 1.0f)
                : math::Vec4(0.20f, 0.20f, 0.24f, 1.0f);
            drawList.AddRect(position, position + size, ApplyVisualColor(color));
            m_Owner.DrawText(drawList, m_Owner.m_Options[m_Index], position);
        }
    private:
        UIComboBox& m_Owner;
        std::size_t m_Index;
    };

    void DrawText(UIDrawList& drawList, const std::string& text, const math::Vec2& position) const
    {
        if (m_Font != nullptr)
        {
            m_Font->AppendText(drawList, text, position + math::Vec2(8.0f, 22.0f),
                m_RowHeight, ApplyVisualColor(math::Vec4(1.0f, 1.0f, 1.0f, 1.0f)));
        }
    }

    void SelectHighlighted()
    {
        if (m_HighlightedIndex >= m_Options.size())
        {
            return;
        }
        const std::size_t index = m_HighlightedIndex;
        Close();
        SetSelectedIndex(index);
    }

    void DestroyPopup()
    {
        UIContext* context = GetContext();
        if (context != nullptr && m_Popup != nullptr)
        {
            context->RemovePopup(m_Popup);
        }
        m_Popup = nullptr;
    }

    Ref<UIFontAtlas> m_Font;
    std::vector<std::string> m_Options;
    SelectionChangedHandler m_OnSelectionChanged;
    UIElement* m_Popup = nullptr;
    std::size_t m_SelectedIndex = NoSelection;
    std::size_t m_HighlightedIndex = NoSelection;
    float m_RowHeight = 30.0f;
};

} // namespace Raven
