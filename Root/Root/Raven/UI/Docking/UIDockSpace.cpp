#include "Raven/UI/Docking/UIDockSpace.h"

#include <charconv>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

namespace Raven
{
namespace
{
// 最前面の装飾専用Element。Hit Testを無効にしてDrop先の探索を妨げません。
class UIDockPreview final : public UIElement
{
protected:
    void OnBuildDrawList(UIDrawList& drawList, const math::Vec2& position) const override
    {
        drawList.AddRect(position,
            math::Vec2(position.x + GetSize().x, position.y + GetSize().y),
            ApplyVisualColor(math::Vec4(0.30f, 0.58f, 0.95f, 0.26f)));
    }
};
} // namespace

UIDockSpace::UIDockSpace()
{
    SetLayoutMode(UILayoutMode::Absolute);
    SetClipChildren(true);
    auto preview = std::make_unique<UIDockPreview>();
    preview->SetHitTestVisible(false);
    preview->SetAffectsParentMeasure(false);
    preview->SetVisible(false);
    m_Preview = AddChild(std::move(preview));
}

bool UIDockSpace::SetPane(std::uint64_t leafId, Scope<UIElement> pane)
{
    const UIDockNode* node = m_Layout.FindNode(leafId);
    if (node == nullptr || node->GetKind() != UIDockNodeKind::Tabs ||
        pane == nullptr || pane->GetParent() != nullptr || pane->GetContext() != nullptr ||
        m_Panes.find(leafId) != m_Panes.end())
    {
        return false;
    }
    pane->SetAffectsParentMeasure(false);
    UIElement* raw = AddChild(std::move(pane));
    if (raw == nullptr)
    {
        return false;
    }
    m_Panes.emplace(leafId, raw);
    BringChildToFront(m_Preview);
    RefreshLayout();
    return true;
}

UIElement* UIDockSpace::GetPane(std::uint64_t leafId) const
{
    const auto it = m_Panes.find(leafId);
    return it == m_Panes.end() ? nullptr : it->second;
}

UITabView* UIDockSpace::CreateTabView(std::uint64_t leafId)
{
    UIDockNode* node = m_Layout.FindNode(leafId);
    if (node == nullptr || node->GetKind() != UIDockNodeKind::Tabs ||
        node->GetTabs()->GetTabCount() != 0u ||
        m_Panes.find(leafId) != m_Panes.end())
    {
        return nullptr;
    }
    auto view = std::make_unique<UITabView>();
    UITabView* raw = view.get();
    // UITabViewが表示とContentの所有権を担当し、Dock Nodeは順序・選択だけを保持します。
    // Close時はViewの内部Content削除が済んでから通知されるため、論理Modelを安全に更新できます。
    raw->SetOnSelectionChanged([this, leafId](std::uint64_t id)
    {
        UIDockNode* current = m_Layout.FindNode(leafId);
        if (current != nullptr && current->GetTabs() != nullptr)
        {
            current->GetTabs()->SelectTab(id);
        }
    });
    raw->SetOnClosed([this, leafId](std::uint64_t id)
    {
        UIDockNode* current = m_Layout.FindNode(leafId);
        if (current != nullptr && current->GetTabs() != nullptr)
        {
            current->GetTabs()->RemoveTab(id);
        }
    });
    raw->SetOnMoved([this, leafId](std::uint64_t id, std::size_t from, std::size_t to)
    {
        (void)from;
        UIDockNode* current = m_Layout.FindNode(leafId);
        if (current != nullptr && current->GetTabs() != nullptr)
        {
            current->GetTabs()->MoveTab(id, to);
        }
    });
    if (SetPane(leafId, std::move(view)) == false)
    {
        return nullptr;
    }
    m_TabViews.emplace(leafId, raw);
    return raw;
}

UITabView* UIDockSpace::GetTabView(std::uint64_t leafId) const
{
    const auto it = m_TabViews.find(leafId);
    return it == m_TabViews.end() ? nullptr : it->second;
}

bool UIDockSpace::AddTab(std::uint64_t leafId, std::uint64_t tabId,
    std::string title, Scope<UIElement> content, bool closable)
{
    UITabView* view = GetTabView(leafId);
    UIDockNode* node = m_Layout.FindNode(leafId);
    if (view == nullptr || node == nullptr || node->GetTabs() == nullptr ||
        content == nullptr || content->GetParent() != nullptr ||
        content->GetContext() != nullptr || tabId == 0u ||
        view->GetModel().FindTab(tabId) != nullptr ||
        node->GetTabs()->FindTab(tabId) != nullptr)
    {
        return false;
    }
    // View追加中に初回選択Callbackが走るため、論理Modelを先に登録します。
    if (node->GetTabs()->AddTab(tabId, title, closable) == false)
    {
        return false;
    }
    if (view->AddTab(tabId, std::move(title), std::move(content), closable) == false)
    {
        node->GetTabs()->RemoveTab(tabId);
        return false;
    }
    return true;
}

bool UIDockSpace::SelectTab(std::uint64_t leafId, std::uint64_t tabId)
{
    UITabView* view = GetTabView(leafId);
    return view != nullptr && view->SelectTab(tabId);
}

bool UIDockSpace::CloseTab(std::uint64_t leafId, std::uint64_t tabId)
{
    UITabView* view = GetTabView(leafId);
    return view != nullptr && view->CloseTab(tabId);
}

bool UIDockSpace::MoveTab(std::uint64_t leafId, std::uint64_t tabId, std::size_t index)
{
    UITabView* view = GetTabView(leafId);
    return view != nullptr && view->MoveTab(tabId, index);
}

UISplitter* UIDockSpace::GetSplitter(std::uint64_t splitId) const
{
    const auto it = m_Splitters.find(splitId);
    return it == m_Splitters.end() ? nullptr : it->second;
}

UIDockNode* UIDockSpace::Split(std::uint64_t leafId, UIDockSplitAxis axis,
    float ratio, bool newLeafFirst)
{
    UIDockNode* fresh = m_Layout.Split(leafId, axis, ratio, newLeafFirst);
    if (fresh != nullptr)
    {
        RefreshLayout();
    }
    return fresh;
}

void UIDockSpace::SetSplitterThickness(float value)
{
    if (std::isfinite(value) == true && value >= 0.0f)
    {
        m_SplitterThickness = value;
        RefreshLayout();
    }
}

void UIDockSpace::SetMinimumPaneExtent(float value)
{
    if (std::isfinite(value) == true && value >= 0.0f)
    {
        m_MinimumPaneExtent = value;
        RefreshLayout();
    }
}

void UIDockSpace::SyncWidgets()
{
    const UIDockRect bounds{ 0.0f, 0.0f, GetSize().x, GetSize().y };
    const auto placements = UIDockGeometry::Calculate(
        m_Layout, bounds, m_SplitterThickness, m_MinimumPaneExtent);
    std::unordered_set<std::uint64_t> active;
    for (const UIDockPlacement& placement : placements)
    {
        if (placement.IsSplit == false)
        {
            continue;
        }
        active.insert(placement.NodeId);
        if (m_Splitters.find(placement.NodeId) != m_Splitters.end())
        {
            continue;
        }
        const std::uint64_t id = placement.NodeId;
        auto splitter = std::make_unique<UISplitter>();
        splitter->SetAffectsParentMeasure(false);
        splitter->SetName("DockSplitter_" + std::to_string(id));
        splitter->SetOnDragDelta([this, id](float delta)
        {
            OnSplitterDrag(id, delta);
        });
        UISplitter* raw = static_cast<UISplitter*>(AddChild(std::move(splitter)));
        if (raw != nullptr)
        {
            m_Splitters.emplace(id, raw);
        }
    }
    // 将来のTree再構築にも備え、消えたSplitの入力CaptureをRemoveChild経由で解除します。
    for (auto it = m_Splitters.begin(); it != m_Splitters.end();)
    {
        if (active.find(it->first) == active.end())
        {
            RemoveChild(it->second);
            it = m_Splitters.erase(it);
        }
        else
        {
            ++it;
        }
    }
    BringChildToFront(m_Preview);
}

void UIDockSpace::ApplyRect(UIElement& element, const UIDockRect& rect)
{
    if (element.GetPosition().x != rect.X || element.GetPosition().y != rect.Y)
    {
        element.SetPosition(math::Vec2(rect.X, rect.Y));
    }
    if (element.GetPreferredSize().x != rect.Width ||
        element.GetPreferredSize().y != rect.Height)
    {
        element.SetSize(math::Vec2(rect.Width, rect.Height));
    }
}

void UIDockSpace::ApplyLayout()
{
    const UIDockRect bounds{ 0.0f, 0.0f, GetSize().x, GetSize().y };
    const auto placements = UIDockGeometry::Calculate(
        m_Layout, bounds, m_SplitterThickness, m_MinimumPaneExtent);
    for (const UIDockPlacement& placement : placements)
    {
        if (placement.IsSplit == true)
        {
            UISplitter* splitter = GetSplitter(placement.NodeId);
            const UIDockNode* node = m_Layout.FindNode(placement.NodeId);
            if (splitter != nullptr && node != nullptr)
            {
                splitter->SetOrientation(node->GetAxis() == UIDockSplitAxis::Horizontal ?
                    UISplitterOrientation::Vertical : UISplitterOrientation::Horizontal);
                ApplyRect(*splitter, placement.Splitter);
            }
        }
        else
        {
            UIElement* pane = GetPane(placement.NodeId);
            if (pane != nullptr)
            {
                ApplyRect(*pane, placement.Bounds);
            }
        }
    }
    if (m_PreviewLeaf != 0u && m_Preview != nullptr)
    {
        for (const UIDockPlacement& placement : placements)
        {
            if (placement.NodeId == m_PreviewLeaf && placement.IsSplit == false)
            {
                ApplyRect(*m_Preview, placement.Bounds);
                break;
            }
        }
    }
}

bool UIDockSpace::RestoreStructure(const std::vector<UIDockLayoutRecord>& records)
{
    if (m_Panes.empty() == false || m_TabViews.empty() == false)
    {
        return false;
    }
    UIDockLayout restored;
    if (restored.RestoreStructure(records) == false)
    {
        return false;
    }
    // 検証済みTreeだけを採用。RefreshLayoutが古いSplitterをRemoveChildし、
    // 新しいNode IDへ対応するSplitterを生成します。
    m_Layout = std::move(restored);
    m_PreviewLeaf = 0u;
    if (m_Preview != nullptr)
    {
        m_Preview->SetVisible(false);
    }
    RefreshLayout();
    return true;
}

bool UIDockSpace::CloseEmptyPane(std::uint64_t leafId)
{
    const UIDockNode* leaf = m_Layout.FindNode(leafId);
    if (leaf == nullptr || leaf->GetTabs() == nullptr ||
        leaf->GetTabs()->GetTabCount() != 0u || leaf->GetParent() == nullptr)
    {
        return false;
    }
    UITabView* view = GetTabView(leafId);
    if (view != nullptr && view->GetModel().GetTabCount() != 0u)
    {
        return false;
    }
    if (m_Layout.RemoveEmptyLeaf(leafId) == false)
    {
        return false;
    }
    // Widgetを破棄する前にraw pointerの登録を外します。RemoveChildはContextの
    // Capture/Focusを解除するため、Drag中のPane削除でも入力参照を残しません。
    m_TabViews.erase(leafId);
    const auto pane = m_Panes.find(leafId);
    if (pane != m_Panes.end())
    {
        UIElement* raw = pane->second;
        m_Panes.erase(pane);
        RemoveChild(raw);
    }
    if (m_PreviewLeaf == leafId)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
    }
    RefreshLayout();
    return true;
}

void UIDockSpace::RefreshLayout()
{
    SyncWidgets();
    ApplyLayout();
}

void UIDockSpace::OnSplitterDrag(std::uint64_t splitId, float delta)
{
    UIDockNode* node = m_Layout.FindNode(splitId);
    if (node == nullptr)
    {
        return;
    }
    const auto placements = UIDockGeometry::Calculate(m_Layout,
        UIDockRect{ 0.0f, 0.0f, GetSize().x, GetSize().y },
        m_SplitterThickness, m_MinimumPaneExtent);
    for (const UIDockPlacement& placement : placements)
    {
        if (placement.NodeId == splitId)
        {
            if (UIDockGeometry::Resize(*node, placement.Bounds, delta,
                m_SplitterThickness, m_MinimumPaneExtent) == true)
            {
                ApplyLayout();
            }
            return;
        }
    }
}

bool UIDockSpace::MoveTabToPane(std::uint64_t sourceLeafId,
    std::uint64_t targetLeafId, std::uint64_t tabId)
{
    if (sourceLeafId == targetLeafId)
    {
        return false;
    }
    UITabView* source = GetTabView(sourceLeafId);
    UITabView* target = GetTabView(targetLeafId);
    UIDockNode* sourceNode = m_Layout.FindNode(sourceLeafId);
    UIDockNode* targetNode = m_Layout.FindNode(targetLeafId);
    if (source == nullptr || target == nullptr || sourceNode == nullptr ||
        targetNode == nullptr || sourceNode->GetTabs() == nullptr ||
        targetNode->GetTabs() == nullptr ||
        target->GetModel().FindTab(tabId) != nullptr ||
        targetNode->GetTabs()->FindTab(tabId) != nullptr)
    {
        return false;
    }
    const UITabItem* item = source->GetModel().FindTab(tabId);
    if (item == nullptr || sourceNode->GetTabs()->FindTab(tabId) == nullptr)
    {
        return false;
    }
    const std::string title = item->Title;
    const bool closable = item->Closable;
    Scope<UIElement> content = source->ExtractTab(tabId);
    if (content == nullptr)
    {
        return false;
    }
    // AddTabの事前条件を検査済み。移動先追加失敗時は元のPaneへ所有権を戻す必要があるため、
    // 現状は通常の整合状態でのみ呼び出す内部移動経路とします。
    return AddTab(targetLeafId, tabId, title, std::move(content), closable);
}

std::uint64_t UIDockSpace::FindSourceLeaf(const UIElement* source) const
{
    for (const auto& entry : m_TabViews)
    {
        if (entry.second != nullptr && entry.second->GetTabBar() == source)
        {
            return entry.first;
        }
    }
    return 0u;
}

std::uint64_t UIDockSpace::FindLeafAt(const math::Vec2& local) const
{
    const auto placements = UIDockGeometry::Calculate(m_Layout,
        UIDockRect{ 0.0f, 0.0f, GetSize().x, GetSize().y },
        m_SplitterThickness, m_MinimumPaneExtent);
    for (const UIDockPlacement& placement : placements)
    {
        const UIDockRect& r = placement.Bounds;
        if (placement.IsSplit == false && local.x >= r.X && local.y >= r.Y &&
            local.x < r.X + r.Width && local.y < r.Y + r.Height &&
            GetTabView(placement.NodeId) != nullptr)
        {
            return placement.NodeId;
        }
    }
    return 0u;
}

bool UIDockSpace::OnDragDropEvent(UIDragDropEvent& event)
{
    if (event.Type == UIDragDropEventType::Leave ||
        event.Type == UIDragDropEventType::Cancel ||
        event.Type == UIDragDropEventType::End)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
        return false;
    }
    if (event.Payload == nullptr || event.Payload->Type != "Raven/UITab")
    {
        return false;
    }
    const std::uint64_t sourceLeaf = FindSourceLeaf(event.Source);
    if (sourceLeaf == 0u)
    {
        return false;
    }
    std::uint64_t tabId = 0u;
    const std::string& data = event.Payload->Data;
    const auto parsed = std::from_chars(data.data(), data.data() + data.size(), tabId);
    if (parsed.ec != std::errc{} || parsed.ptr != data.data() + data.size() ||
        tabId == 0u)
    {
        return false;
    }
    math::Vec2 local;
    if (TryScreenToLocalPosition(event.ScreenPosition, local) == false)
    {
        return false;
    }
    const std::uint64_t targetLeaf = FindLeafAt(local);
    if (targetLeaf == 0u || targetLeaf == sourceLeaf ||
        GetTabView(sourceLeaf)->GetModel().FindTab(tabId) == nullptr ||
        GetTabView(targetLeaf)->GetModel().FindTab(tabId) != nullptr)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
        return false;
    }
    if (event.Type == UIDragDropEventType::Over ||
        event.Type == UIDragDropEventType::Enter)
    {
        m_PreviewLeaf = targetLeaf;
        ApplyLayout();
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(true);
        }
        event.Accepted = true;
        return true;
    }
    if (event.Type == UIDragDropEventType::Drop)
    {
        m_PreviewLeaf = 0u;
        if (m_Preview != nullptr)
        {
            m_Preview->SetVisible(false);
        }
        return MoveTabToPane(sourceLeaf, targetLeaf, tabId);
    }
    return false;
}

void UIDockSpace::OnBuildDrawList(UIDrawList& drawList,
    const math::Vec2& absolutePosition) const
{
    (void)drawList;
    (void)absolutePosition;
    // 親のArrangeで実Sizeが確定するため、外部Resize後の次frameへ配置を反映します。
    // UIElement::BuildDrawListは非virtualなので、描画中のTree追加を避け、
    // Splitter生成はSplit()/RefreshLayout()からのみ実施します。
    const_cast<UIDockSpace*>(this)->ApplyLayout();
}

} // namespace Raven
