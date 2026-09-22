#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{

// Tabの表示順・選択状態を保持するUI非依存のモデルです。
// Docking時にも同じモデルを利用できるよう、UIElementや描画処理への依存を持たせません。
// ID=0は「選択なし」の予約値です。Indexは並び替えで変化するため、外部参照にはIDを使用してください。
struct UITabItem
{
    std::uint64_t Id = 0u;
    std::string Title;
    bool Closable = true;
};

class UITabModel final
{
public:
    using SelectionHandler = std::function<void(std::uint64_t)>;
    using ClosedHandler = std::function<void(std::uint64_t)>;
    using MovedHandler = std::function<void(std::uint64_t, std::size_t, std::size_t)>;

    // ID重複と予約値を拒否します。最初のTabは自動選択します。
    bool AddTab(std::uint64_t id, std::string title, bool closable = true)
    {
        if (id == 0u || FindTab(id) != nullptr)
        {
            return false;
        }
        m_Tabs.push_back(UITabItem{ id, std::move(title), closable });
        if (m_SelectedId == 0u)
        {
            SelectTab(id);
        }
        return true;
    }

    const UITabItem* FindTab(std::uint64_t id) const
    {
        const auto it = std::find_if(m_Tabs.begin(), m_Tabs.end(),
            [id](const UITabItem& tab) { return tab.Id == id; });
        return it != m_Tabs.end() ? &*it : nullptr;
    }

    const std::vector<UITabItem>& GetTabs() const { return m_Tabs; }
    std::size_t GetTabCount() const { return m_Tabs.size(); }
    std::uint64_t GetSelectedTabId() const { return m_SelectedId; }

    bool SelectTab(std::uint64_t id)
    {
        if (id != 0u && FindTab(id) == nullptr)
        {
            return false;
        }
        if (id == 0u && m_Tabs.empty() == false)
        {
            return false;
        }
        if (m_SelectedId == id)
        {
            return true;
        }
        m_SelectedId = id;
        if (m_OnSelectionChanged)
        {
            m_OnSelectionChanged(id);
        }
        return true;
    }

    // Close操作ではClosableを尊重します。強制削除が必要な場合はRemoveTabを使用します。
    bool CloseTab(std::uint64_t id)
    {
        const UITabItem* tab = FindTab(id);
        if (tab == nullptr || tab->Closable == false)
        {
            return false;
        }
        return RemoveTab(id);
    }

    bool RemoveTab(std::uint64_t id)
    {
        const auto it = std::find_if(m_Tabs.begin(), m_Tabs.end(),
            [id](const UITabItem& tab) { return tab.Id == id; });
        if (it == m_Tabs.end())
        {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(it - m_Tabs.begin());
        const bool selected = m_SelectedId == id;
        m_Tabs.erase(it);
        if (selected == true)
        {
            // 選択Tabを閉じた場合は同じ位置の右隣、なければ左隣を選択します。
            // Callbackはモデルの状態が確定した後に通知します。
            m_SelectedId = m_Tabs.empty() ? 0u
                : m_Tabs[std::min(index, m_Tabs.size() - 1u)].Id;
        }
        if (m_OnClosed)
        {
            m_OnClosed(id);
        }
        if (selected == true && m_OnSelectionChanged)
        {
            m_OnSelectionChanged(m_SelectedId);
        }
        return true;
    }

    // toIndexは移動完了後のIndexです。隣接/同位置の無変更移動では通知しません。
    bool MoveTab(std::uint64_t id, std::size_t toIndex)
    {
        if (toIndex >= m_Tabs.size())
        {
            return false;
        }
        const auto it = std::find_if(m_Tabs.begin(), m_Tabs.end(),
            [id](const UITabItem& tab) { return tab.Id == id; });
        if (it == m_Tabs.end())
        {
            return false;
        }
        const std::size_t fromIndex = static_cast<std::size_t>(it - m_Tabs.begin());
        if (fromIndex == toIndex)
        {
            return true;
        }
        // std::rotateは移動範囲だけを変更し、他Tabの相対順序を維持します。
        if (fromIndex < toIndex)
        {
            std::rotate(m_Tabs.begin() + fromIndex,
                m_Tabs.begin() + fromIndex + 1u, m_Tabs.begin() + toIndex + 1u);
        }
        else
        {
            std::rotate(m_Tabs.begin() + toIndex,
                m_Tabs.begin() + fromIndex, m_Tabs.begin() + fromIndex + 1u);
        }
        if (m_OnMoved)
        {
            m_OnMoved(id, fromIndex, toIndex);
        }
        return true;
    }

    void Clear()
    {
        if (m_Tabs.empty() == true)
        {
            return;
        }
        m_Tabs.clear();
        const bool hadSelection = m_SelectedId != 0u;
        m_SelectedId = 0u;
        if (hadSelection == true && m_OnSelectionChanged)
        {
            m_OnSelectionChanged(0u);
        }
    }

    void SetOnSelectionChanged(SelectionHandler handler) { m_OnSelectionChanged = std::move(handler); }
    void SetOnClosed(ClosedHandler handler) { m_OnClosed = std::move(handler); }
    void SetOnMoved(MovedHandler handler) { m_OnMoved = std::move(handler); }

private:
    std::vector<UITabItem> m_Tabs;
    std::uint64_t m_SelectedId = 0u;
    SelectionHandler m_OnSelectionChanged;
    ClosedHandler m_OnClosed;
    MovedHandler m_OnMoved;
};

} // namespace Raven
