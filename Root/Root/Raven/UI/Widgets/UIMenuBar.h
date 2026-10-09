#pragma once

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Text/UIFontAtlas.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIPanel.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{

// UIContextのPopup Layerを使うRetained Menu Barです。
// PopupはContext所有、TriggerはMenuBar所有とし、Detach時にPopupを必ず回収します。
class UIMenuBar final : public UIElement
{
public:
    using Action = std::function<void()>;

    UIMenuBar()
    {
        SetLayoutMode(UILayoutMode::Horizontal);
    }

    ~UIMenuBar() override
    {
        DestroyPopups(GetContext());
    }

    void SetFont(const Ref<UIFontAtlas>& font) { m_Font = font; }

    // Menu構築はContext接続前に完了させます。動的追加はPopup再構築が必要なため未対応です。
    std::size_t AddMenu(std::string title)
    {
        const std::size_t index = m_Menus.size();
        auto trigger = CreateScope<UIButton>();
        trigger->SetFocusable(true);
        trigger->SetPreferredSize(math::Vec2(100.0f, 34.0f));
        trigger->SetSize(math::Vec2(100.0f, 34.0f));
        trigger->SetOnClick([this, index]() { ToggleMenu(index); });
        auto label = CreateScope<UILabel>();
        label->SetFont(m_Font);
        label->SetText(std::move(title));
        label->SetPosition(math::Vec2(12.0f, 3.0f));
        label->SetSize(math::Vec2(84.0f, 26.0f));
        label->SetHitTestVisible(false);
        trigger->AddChild(std::move(label));
        UIElement* attached = AddChild(std::move(trigger));
        m_Menus.push_back(Menu{ attached, nullptr, {} });
        return index;
    }

    bool AddItem(std::size_t menuIndex, std::string title, Action action)
    {
        if (menuIndex >= m_Menus.size() || m_Menus[menuIndex].Popup != nullptr)
        {
            return false;
        }
        m_Menus[menuIndex].Items.push_back(Item{ std::move(title), std::move(action) });
        return true;
    }

    bool ToggleMenu(std::size_t menuIndex)
    {
        UIContext* context = GetContext();
        if (context == nullptr || menuIndex >= m_Menus.size())
        {
            return false;
        }
        Menu& menu = m_Menus[menuIndex];
        if (context->GetOpenPopup() == menu.Popup && menu.Popup != nullptr)
        {
            context->ClosePopup();
            return true;
        }
        if (menu.Items.empty())
        {
            return false;
        }
        if (menu.Popup == nullptr)
        {
            auto popup = CreateScope<UIPanel>();
            popup->SetBackgroundColor(math::Vec4(0.16f, 0.17f, 0.21f, 1.0f));
            const float height = 12.0f + static_cast<float>(menu.Items.size()) * 38.0f;
            popup->SetPreferredSize(math::Vec2(220.0f, height));
            popup->SetSize(math::Vec2(220.0f, height));
            for (std::size_t i = 0u; i < menu.Items.size(); ++i)
            {
                auto button = CreateScope<UIButton>();
                button->SetFocusable(true);
                button->SetPosition(math::Vec2(6.0f, 6.0f + static_cast<float>(i) * 38.0f));
                button->SetSize(math::Vec2(208.0f, 32.0f));
                button->SetOnClick([this, menuIndex, i]()
                    {
                        // ClosePopupがFocusを解除するため、Action実行前に閉じます。
                        UIContext* active = GetContext();
                        if (active != nullptr)
                        {
                            active->ClosePopup();
                        }
                        Action action = m_Menus[menuIndex].Items[i].Callback;
                        if (action != nullptr)
                        {
                            action();
                        }
                    });
                auto label = CreateScope<UILabel>();
                label->SetFont(m_Font);
                label->SetText(menu.Items[i].Title);
                label->SetPosition(math::Vec2(10.0f, 3.0f));
                label->SetSize(math::Vec2(188.0f, 26.0f));
                label->SetHitTestVisible(false);
                button->AddChild(std::move(label));
                popup->AddChild(std::move(button));
            }
            menu.Popup = context->AddPopup(std::move(popup));
        }
        return menu.Popup != nullptr && context->OpenPopupAt(menu.Popup, menu.Trigger);
    }

protected:
    void OnContextChanged(UIContext* previous, UIContext* current) override
    {
        if (previous != nullptr && previous != current)
        {
            DestroyPopups(previous);
        }
    }

private:
    struct Item
    {
        std::string Title;
        Action Callback;
    };
    struct Menu
    {
        UIElement* Trigger = nullptr;
        UIElement* Popup = nullptr;
        std::vector<Item> Items;
    };

    void DestroyPopups(UIContext* context)
    {
        if (context == nullptr)
        {
            return;
        }
        for (Menu& menu : m_Menus)
        {
            if (menu.Popup != nullptr)
            {
                context->RemovePopup(menu.Popup);
                menu.Popup = nullptr;
            }
        }
    }

    Ref<UIFontAtlas> m_Font;
    std::vector<Menu> m_Menus;
};

} // namespace Raven
