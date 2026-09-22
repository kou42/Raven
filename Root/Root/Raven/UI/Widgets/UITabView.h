#pragma once

#include "Raven/UI/Core/UIElement.h"
#include "Raven/UI/Widgets/UITabBar.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{

// Tab HeaderとContentの所有権をまとめるRetained Widgetです。
// ModelはViewが所有し、選択状態と表示Contentの同期を一箇所に集約します。
class UITabView final : public UIElement
{
public:
    using SelectionHandler = UITabModel::SelectionHandler;
    using ClosedHandler = UITabModel::ClosedHandler;
    using MovedHandler = UITabModel::MovedHandler;

    UITabView();

    bool AddTab(std::uint64_t id, std::string title, Scope<UIElement> content,
        bool closable = true);
    bool SelectTab(std::uint64_t id) { return m_Model.SelectTab(id); }
    bool CloseTab(std::uint64_t id) { return m_Model.CloseTab(id); }
    bool RemoveTab(std::uint64_t id) { return m_Model.RemoveTab(id); }
    bool MoveTab(std::uint64_t id, std::size_t index) { return m_Model.MoveTab(id, index); }

    const UITabModel& GetModel() const { return m_Model; }
    UITabBar* GetTabBar() { return m_TabBar; }
    const UITabBar* GetTabBar() const { return m_TabBar; }
    UIElement* GetTabContent(std::uint64_t id);
    const UIElement* GetTabContent(std::uint64_t id) const;

    void SetOnSelectionChanged(SelectionHandler handler) { m_OnSelectionChanged = std::move(handler); }
    void SetOnClosed(ClosedHandler handler) { m_OnClosed = std::move(handler); }
    void SetOnMoved(MovedHandler handler) { m_OnMoved = std::move(handler); }

private:
    struct Page
    {
        std::uint64_t Id = 0u;
        UIElement* Content = nullptr;
    };

    void SyncContentVisibility();
    void RemoveContent(std::uint64_t id);

    UITabModel m_Model;
    UITabBar* m_TabBar = nullptr;
    UIElement* m_ContentHost = nullptr;
    std::vector<Page> m_Pages;
    SelectionHandler m_OnSelectionChanged;
    ClosedHandler m_OnClosed;
    MovedHandler m_OnMoved;
};

} // namespace Raven
