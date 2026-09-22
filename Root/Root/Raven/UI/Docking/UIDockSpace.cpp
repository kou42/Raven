#include "Raven/UI/Docking/UIDockSpace.h"

#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

namespace Raven
{

UIDockSpace::UIDockSpace()
{
    SetLayoutMode(UILayoutMode::Absolute);
    SetClipChildren(true);
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
    RefreshLayout();
    return true;
}

UIElement* UIDockSpace::GetPane(std::uint64_t leafId) const
{
    const auto it = m_Panes.find(leafId);
    return it == m_Panes.end() ? nullptr : it->second;
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
