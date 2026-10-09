#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Widgets/UIButton.h"

#include <utility>

namespace Raven
{

// Editor Inspectorの折りたたみSectionです。
// HeaderとContentの所有権はこのWidgetに集約し、閉じたContentは
// 描画・Hit Test・Layoutの対象から除外します。
class UICollapsibleSection final : public UIElement
{
public:
    UICollapsibleSection()
    {
        SetLayoutMode(UILayoutMode::Vertical);
        auto header = CreateScope<UIButton>();
        header->SetFocusable(true);
        header->SetPreferredSize(math::Vec2(260.0f, 32.0f));
        header->SetOnClick([this]() { SetExpanded(m_Expanded == false); });
        m_Header = static_cast<UIButton*>(AddChild(std::move(header)));

        auto content = CreateScope<UIElement>();
        content->SetLayoutMode(UILayoutMode::Vertical);
        m_Content = AddChild(std::move(content));
    }

    bool IsExpanded() const { return m_Expanded; }

    void SetExpanded(bool expanded)
    {
        if (m_Expanded == expanded)
        {
            return;
        }
        m_Expanded = expanded;
        if (m_Content != nullptr)
        {
            m_Content->SetVisible(expanded);
        }
    }

    // Headerの文字・アイコンはChildとして追加し、装飾側のHit Testを無効化します。
    UIButton* GetHeader() { return m_Header; }
    const UIButton* GetHeader() const { return m_Header; }

    UIElement* GetContent() { return m_Content; }
    const UIElement* GetContent() const { return m_Content; }

private:
    bool m_Expanded = true;
    UIButton* m_Header = nullptr;
    UIElement* m_Content = nullptr;
};

} // namespace Raven
