#include "Raven/UI/Widgets/UITabView.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace Raven
{

UITabView::UITabView()
{
    SetLayoutMode(UILayoutMode::Vertical);
    SetClipChildren(true);
    SetPreferredSize(math::Vec2(300.0f, 270.0f));

    auto bar = std::make_unique<UITabBar>(m_Model);
    bar->SetHorizontalAlignment(UIAlignment::Stretch);
    m_TabBar = static_cast<UITabBar*>(AddChild(std::move(bar)));

    auto host = std::make_unique<UIElement>();
    host->SetLayoutMode(UILayoutMode::Vertical);
    host->SetHorizontalAlignment(UIAlignment::Stretch);
    host->SetPreferredSize(math::Vec2(300.0f, 240.0f));
    host->SetClipChildren(true);
    m_ContentHost = AddChild(std::move(host));

    // Modelからの通知を先にView内部へ反映し、外部Callbackからは同期済みTreeを観測できるようにします。
    m_Model.SetOnSelectionChanged([this](std::uint64_t id)
    {
        SyncContentVisibility();
        m_TabBar->EnsureTabVisible(id);
        if (m_OnSelectionChanged)
        {
            m_OnSelectionChanged(id);
        }
    });
    m_Model.SetOnClosed([this](std::uint64_t id)
    {
        RemoveContent(id);
        m_TabBar->SetScrollOffset(m_TabBar->GetScrollOffset());
        if (m_OnClosed)
        {
            m_OnClosed(id);
        }
    });
    m_Model.SetOnMoved([this](std::uint64_t id, std::size_t from, std::size_t to)
    {
        if (m_OnMoved)
        {
            m_OnMoved(id, from, to);
        }
    });
}

bool UITabView::AddTab(std::uint64_t id, std::string title,
    Scope<UIElement> content, bool closable)
{
    if (content == nullptr || content->GetParent() != nullptr ||
        content->GetContext() != nullptr || m_Model.FindTab(id) != nullptr || id == 0u)
    {
        return false;
    }
    // 非選択Pageは追加前に非表示とし、初回Measure/Hit Testに参加させません。
    content->SetVisible(false);
    content->SetHorizontalAlignment(UIAlignment::Stretch);
    UIElement* page = m_ContentHost->AddChild(std::move(content));
    if (page == nullptr)
    {
        return false;
    }
    m_Pages.push_back(Page{ id, page });
    if (m_Model.AddTab(id, std::move(title), closable) == false)
    {
        RemoveContent(id);
        return false;
    }
    SyncContentVisibility();
    return true;
}

Scope<UIElement> UITabView::ExtractTab(std::uint64_t id)
{
    const auto it = std::find_if(m_Pages.begin(), m_Pages.end(),
        [id](const Page& page) { return page.Id == id; });
    if (it == m_Pages.end() || m_Model.FindTab(id) == nullptr)
    {
        return nullptr;
    }
    // ModelのClosed通知は通常Contentを破棄します。先にPageを管理対象から外して
    // DetachChildで所有権を回収し、移動先のAddTabへそのまま渡します。
    Scope<UIElement> content = m_ContentHost->DetachChild(it->Content);
    if (content == nullptr)
    {
        return nullptr;
    }
    m_Pages.erase(it);
    m_Model.RemoveTab(id);
    return content;
}

UIElement* UITabView::GetTabContent(std::uint64_t id)
{
    const auto it = std::find_if(m_Pages.begin(), m_Pages.end(),
        [id](const Page& page) { return page.Id == id; });
    return it != m_Pages.end() ? it->Content : nullptr;
}

const UIElement* UITabView::GetTabContent(std::uint64_t id) const
{
    return const_cast<UITabView*>(this)->GetTabContent(id);
}

void UITabView::SyncContentVisibility()
{
    const std::uint64_t selectedId = m_Model.GetSelectedTabId();
    for (const Page& page : m_Pages)
    {
        if (page.Content != nullptr)
        {
            // SetVisible(false)はUIContextのCapture/Focusも安全に解除します。
            page.Content->SetVisible(page.Id == selectedId);
        }
    }
}

void UITabView::RemoveContent(std::uint64_t id)
{
    const auto it = std::find_if(m_Pages.begin(), m_Pages.end(),
        [id](const Page& page) { return page.Id == id; });
    if (it == m_Pages.end())
    {
        return;
    }
    // UIElement::RemoveChildがContextへSubtree削除を通知してから所有権を解放します。
    m_ContentHost->RemoveChild(it->Content);
    m_Pages.erase(it);
}

} // namespace Raven
