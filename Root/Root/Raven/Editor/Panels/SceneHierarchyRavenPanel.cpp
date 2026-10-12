#include "Raven/Editor/Panels/SceneHierarchyRavenPanel.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIInputText.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UITreeView.h"
#include "Raven/UI/Widgets/UIWindow.h"

#include <unordered_set>
#include <utility>

namespace Raven
{

bool SceneHierarchyRavenPanel::Attach(
    UIContext& context, const Ref<UIFontAtlas>& font)
{
    if (m_Root != nullptr)
    {
        return false;
    }

    auto window = CreateScope<UIWindow>();
    window->SetTitle("Scene Hierarchy");
    window->SetPosition(math::Vec2(24.0f, 24.0f));
    window->SetSize(math::Vec2(360.0f, 620.0f));
    window->SetPreferredSize(math::Vec2(360.0f, 620.0f));

    auto title = CreateScope<UILabel>();
    title->SetFont(font);
    title->SetText("Scene Hierarchy");
    title->SetPosition(math::Vec2(12.0f, 3.0f));
    title->SetSize(math::Vec2(320.0f, 24.0f));
    title->SetHitTestVisible(false);
    window->AddChild(std::move(title));

    auto createButton = CreateScope<UIButton>();
    createButton->SetPosition(math::Vec2(10.0f, 34.0f));
    createButton->SetSize(math::Vec2(132.0f, 30.0f));
    createButton->SetPreferredSize(math::Vec2(132.0f, 30.0f));
    createButton->SetOnClick([this]()
        {
            if (m_OnCreateRequested != nullptr)
            {
                m_OnCreateRequested();
            }
        });
    auto createCaption = CreateScope<UILabel>();
    createCaption->SetFont(font);
    createCaption->SetText("Create Entity");
    createCaption->SetPosition(math::Vec2(10.0f, 4.0f));
    createCaption->SetSize(math::Vec2(112.0f, 22.0f));
    createCaption->SetHitTestVisible(false);
    createButton->AddChild(std::move(createCaption));
    m_CreateButton = window->AddChild(std::move(createButton));

    auto deleteButton = CreateScope<UIButton>();
    deleteButton->SetPosition(math::Vec2(148.0f, 34.0f));
    deleteButton->SetSize(math::Vec2(96.0f, 30.0f));
    deleteButton->SetPreferredSize(math::Vec2(96.0f, 30.0f));
    deleteButton->SetOnClick([this]()
        {
            if (m_OnDeleteRequested != nullptr
                && static_cast<bool>(m_Snapshot.Selected) == true)
            {
                m_OnDeleteRequested(m_Snapshot.Selected);
            }
        });
    auto deleteCaption = CreateScope<UILabel>();
    deleteCaption->SetFont(font);
    deleteCaption->SetText("Delete");
    deleteCaption->SetPosition(math::Vec2(18.0f, 4.0f));
    deleteCaption->SetSize(math::Vec2(60.0f, 22.0f));
    deleteCaption->SetHitTestVisible(false);
    deleteButton->AddChild(std::move(deleteCaption));
    m_DeleteButton = window->AddChild(std::move(deleteButton));

    auto status = CreateScope<UILabel>();
    status->SetFont(font);
    status->SetPosition(math::Vec2(252.0f, 38.0f));
    status->SetSize(math::Vec2(98.0f, 24.0f));
    status->SetHitTestVisible(false);
    m_StatusLabel = static_cast<UILabel*>(window->AddChild(std::move(status)));

    auto renameLabel = CreateScope<UILabel>();
    renameLabel->SetFont(font);
    renameLabel->SetText("Name");
    renameLabel->SetPosition(math::Vec2(10.0f, 75.0f));
    renameLabel->SetSize(math::Vec2(54.0f, 28.0f));
    renameLabel->SetHitTestVisible(false);
    m_RenameLabel = static_cast<UILabel*>(window->AddChild(std::move(renameLabel)));

    auto renameInput = CreateScope<UIInputText>();
    renameInput->SetFont(font);
    renameInput->SetPosition(math::Vec2(68.0f, 70.0f));
    renameInput->SetSize(math::Vec2(282.0f, 30.0f));
    renameInput->SetPreferredSize(math::Vec2(282.0f, 30.0f));
    renameInput->SetOnSubmit([this]() { CommitRename(); });
    renameInput->SetOnFocusLost([this]() { CommitRename(); });
    m_RenameInput = static_cast<UIInputText*>(window->AddChild(std::move(renameInput)));

    auto tree = CreateScope<UITreeView>();
    tree->SetFont(font);
    tree->SetPosition(math::Vec2(10.0f, 108.0f));
    tree->SetSize(math::Vec2(340.0f, 500.0f));
    tree->SetPreferredSize(math::Vec2(340.0f, 500.0f));
    tree->SetOnSelectionChanged([this](std::uint64_t id)
        {
            OnSelectionChanged(id);
        });
    tree->SetOnExpansionChanged([this](std::uint64_t id, bool expanded)
        {
            m_ExpandedStates[id] = expanded;
        });
    m_Tree = static_cast<UITreeView*>(window->AddChild(std::move(tree)));

    m_Context = &context;
    m_Root = context.GetRootElement().AddChild(std::move(window));
    if (m_Root == nullptr)
    {
        m_Context = nullptr;
        m_CreateButton = nullptr;
        m_DeleteButton = nullptr;
        m_StatusLabel = nullptr;
        m_RenameLabel = nullptr;
        m_RenameInput = nullptr;
        m_Tree = nullptr;
        return false;
    }
    Update(SceneHierarchySnapshot{});
    return true;
}

void SceneHierarchyRavenPanel::Detach()
{
    if (m_Context != nullptr && m_Root != nullptr)
    {
        m_Context->GetRootElement().RemoveChild(m_Root);
    }
    m_Context = nullptr;
    m_Root = nullptr;
    m_CreateButton = nullptr;
    m_DeleteButton = nullptr;
    m_StatusLabel = nullptr;
    m_RenameLabel = nullptr;
    m_RenameInput = nullptr;
    m_Tree = nullptr;
    m_Snapshot = SceneHierarchySnapshot{};
    m_ExpandedStates.clear();
    m_RenameHandle = InvalidEntityHandle;
    m_SynchronizingSelection = false;
}

void SceneHierarchyRavenPanel::Update(const SceneHierarchySnapshot& snapshot)
{
    const bool rebuild = HasSameNodes(snapshot) == false;
    m_Snapshot = snapshot;
    if (m_StatusLabel != nullptr)
    {
        m_StatusLabel->SetText(snapshot.SceneAvailable == true
            ? std::to_string(snapshot.Nodes.size()) + " entities"
            : "No active scene.");
    }
    if (m_CreateButton != nullptr)
    {
        m_CreateButton->SetVisible(snapshot.SceneAvailable);
    }
    if (m_DeleteButton != nullptr)
    {
        m_DeleteButton->SetVisible(
            snapshot.SceneAvailable == true
            && static_cast<bool>(snapshot.Selected) == true);
    }
    if (m_Tree != nullptr)
    {
        m_Tree->SetVisible(snapshot.SceneAvailable);
    }
    if (rebuild == true)
    {
        RebuildTree();
    }
    SynchronizeSelection();
    SynchronizeRenameEditor();
}

void SceneHierarchyRavenPanel::SetVisible(bool visible)
{
    if (m_Root != nullptr)
    {
        m_Root->SetVisible(visible);
    }
}

void SceneHierarchyRavenPanel::SetSelectionRequestedHandler(
    SelectionRequestedHandler handler)
{
    m_OnSelectionRequested = std::move(handler);
}

void SceneHierarchyRavenPanel::SetCreateRequestedHandler(
    CreateRequestedHandler handler)
{
    m_OnCreateRequested = std::move(handler);
}

void SceneHierarchyRavenPanel::SetDeleteRequestedHandler(
    DeleteRequestedHandler handler)
{
    m_OnDeleteRequested = std::move(handler);
}

void SceneHierarchyRavenPanel::SetRenameRequestedHandler(
    RenameRequestedHandler handler)
{
    m_OnRenameRequested = std::move(handler);
}

bool SceneHierarchyRavenPanel::IsAttached() const
{
    return m_Root != nullptr;
}

bool SceneHierarchyRavenPanel::IsVisible() const
{
    return m_Root != nullptr && m_Root->IsVisible() == true;
}

std::size_t SceneHierarchyRavenPanel::GetNodeCount() const
{
    return m_Snapshot.Nodes.size();
}

EntityHandle SceneHierarchyRavenPanel::GetSelectedHandle() const
{
    if (m_Tree == nullptr || m_Tree->GetSelectedNode() == nullptr)
    {
        return InvalidEntityHandle;
    }
    return EntityHandle::FromValue(m_Tree->GetSelectedNode()->Id);
}

bool SceneHierarchyRavenPanel::HasSameNodes(
    const SceneHierarchySnapshot& snapshot) const
{
    if (m_Snapshot.SceneAvailable != snapshot.SceneAvailable ||
        m_Snapshot.Nodes.size() != snapshot.Nodes.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < snapshot.Nodes.size(); ++index)
    {
        const SceneHierarchyNodeSnapshot& before = m_Snapshot.Nodes[index];
        const SceneHierarchyNodeSnapshot& after = snapshot.Nodes[index];
        if (before.Handle != after.Handle || before.Parent != after.Parent ||
            before.DisplayName != after.DisplayName)
        {
            return false;
        }
    }
    return true;
}

void SceneHierarchyRavenPanel::RebuildTree()
{
    if (m_Tree == nullptr)
    {
        return;
    }

    m_SynchronizingSelection = true;
    m_Tree->Clear();
    std::unordered_set<std::uint64_t> added;
    added.reserve(m_Snapshot.Nodes.size());

    // 親が追加済みになったNodeから順に構築します。Snapshot側で循環と孤児を
    // Rootへ正規化しているため、入力順が親子順でなくても有限回で完了します。
    bool progressed = true;
    while (added.size() < m_Snapshot.Nodes.size() && progressed == true)
    {
        progressed = false;
        for (const SceneHierarchyNodeSnapshot& node : m_Snapshot.Nodes)
        {
            if (added.find(node.Handle.Value()) != added.end())
            {
                continue;
            }
            UITreeNode* parent = nullptr;
            if (static_cast<bool>(node.Parent) == true)
            {
                parent = m_Tree->FindNode(node.Parent.Value());
                if (parent == nullptr)
                {
                    continue;
                }
            }
            if (m_Tree->AddNode(parent, node.Handle.Value(), node.DisplayName) != nullptr)
            {
                added.insert(node.Handle.Value());
                progressed = true;
            }
        }
    }

    // Build側の契約が破られてもEntityを消さず、安全なRootとして表示します。
    for (const SceneHierarchyNodeSnapshot& node : m_Snapshot.Nodes)
    {
        if (added.find(node.Handle.Value()) == added.end())
        {
            m_Tree->AddRoot(node.Handle.Value(), node.DisplayName);
        }
    }
    for (const auto& state : m_ExpandedStates)
    {
        UITreeNode* node = m_Tree->FindNode(state.first);
        if (node != nullptr && state.second == false)
        {
            m_Tree->SetExpanded(node, false);
        }
    }
    m_SynchronizingSelection = false;
}

void SceneHierarchyRavenPanel::SynchronizeSelection()
{
    if (m_Tree == nullptr)
    {
        return;
    }
    UITreeNode* selected = static_cast<bool>(m_Snapshot.Selected) == true
        ? m_Tree->FindNode(m_Snapshot.Selected.Value())
        : nullptr;
    m_SynchronizingSelection = true;
    m_Tree->Select(selected);
    m_SynchronizingSelection = false;
}

void SceneHierarchyRavenPanel::SynchronizeRenameEditor()
{
    if (m_RenameInput == nullptr || m_RenameLabel == nullptr)
    {
        return;
    }

    const SceneHierarchyNodeSnapshot* selectedNode = nullptr;
    for (const SceneHierarchyNodeSnapshot& node : m_Snapshot.Nodes)
    {
        if (node.Handle == m_Snapshot.Selected)
        {
            selectedNode = &node;
            break;
        }
    }

    const bool hasSelection = selectedNode != nullptr;
    m_RenameLabel->SetVisible(hasSelection);
    m_RenameInput->SetVisible(hasSelection);
    if (hasSelection == false)
    {
        m_RenameHandle = InvalidEntityHandle;
        m_RenameInput->SetText({});
        return;
    }

    // 入力中は毎FrameのSnapshot同期でCaretや未確定文字を壊しません。
    // 選択Entityが変わった場合だけ、新しいEntity名へ即座に切り替えます。
    const bool selectionChanged = m_RenameHandle != selectedNode->Handle;
    const bool editing = m_Context != nullptr &&
        m_Context->GetFocusedElement() == m_RenameInput;
    m_RenameHandle = selectedNode->Handle;
    if (selectionChanged == true || editing == false)
    {
        if (m_RenameInput->GetText() != selectedNode->DisplayName)
        {
            m_RenameInput->SetText(selectedNode->DisplayName);
        }
    }
}

void SceneHierarchyRavenPanel::CommitRename()
{
    if (m_RenameInput == nullptr ||
        static_cast<bool>(m_RenameHandle) == false ||
        m_OnRenameRequested == nullptr)
    {
        return;
    }

    const std::string requestedName = m_RenameInput->GetText();
    if (m_OnRenameRequested(m_RenameHandle, requestedName) == false)
    {
        // Scene交換、Entity削除、同値入力などでCommandが成立しない場合は、
        // 次FrameのSnapshot同期で正規名へ戻せるようFocusを奪わず処理を終えます。
        return;
    }

    if (m_StatusLabel != nullptr)
    {
        m_StatusLabel->SetText("Renamed.");
    }
}

void SceneHierarchyRavenPanel::OnSelectionChanged(std::uint64_t id)
{
    if (m_SynchronizingSelection == true || m_OnSelectionRequested == nullptr)
    {
        return;
    }
    m_OnSelectionRequested(id != 0u
        ? EntityHandle::FromValue(id)
        : InvalidEntityHandle);
}

} // namespace Raven
