#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Editor/Panels/SceneHierarchySnapshot.h"

#include <cstddef>
#include <functional>
#include <unordered_map>

namespace Raven
{

class UIContext;
class UIElement;
class UIFontAtlas;
class UIInputText;
class UILabel;
class UITreeView;

// Snapshotだけを保持するRaven UI版Scene Hierarchyです。
// Sceneと選択Entityの所有権はEditorLayerに残し、操作はHandle付きActionとして返します。
class SceneHierarchyRavenPanel
{
public:
    using SelectionRequestedHandler = std::function<void(EntityHandle)>;
    using CreateRequestedHandler = std::function<void()>;
    using DeleteRequestedHandler = std::function<void(EntityHandle)>;
    using RenameRequestedHandler =
        std::function<bool(EntityHandle, const std::string&)>;

    bool Attach(UIContext& context, const Ref<UIFontAtlas>& font);
    void Detach();
    void Update(const SceneHierarchySnapshot& snapshot);
    void SetVisible(bool visible);
    void SetSelectionRequestedHandler(SelectionRequestedHandler handler);
    void SetCreateRequestedHandler(CreateRequestedHandler handler);
    void SetDeleteRequestedHandler(DeleteRequestedHandler handler);
    void SetRenameRequestedHandler(RenameRequestedHandler handler);

    bool IsAttached() const;
    bool IsVisible() const;
    std::size_t GetNodeCount() const;
    EntityHandle GetSelectedHandle() const;

private:
    bool HasSameNodes(const SceneHierarchySnapshot& snapshot) const;
    void RebuildTree();
    void SynchronizeSelection();
    void SynchronizeRenameEditor();
    void CommitRename();
    void OnSelectionChanged(std::uint64_t id);

    UIContext* m_Context = nullptr;
    UIElement* m_Root = nullptr;
    UIElement* m_CreateButton = nullptr;
    UIElement* m_DeleteButton = nullptr;
    UILabel* m_StatusLabel = nullptr;
    UILabel* m_RenameLabel = nullptr;
    UIInputText* m_RenameInput = nullptr;
    UITreeView* m_Tree = nullptr;
    SceneHierarchySnapshot m_Snapshot;
    SelectionRequestedHandler m_OnSelectionRequested;
    CreateRequestedHandler m_OnCreateRequested;
    DeleteRequestedHandler m_OnDeleteRequested;
    RenameRequestedHandler m_OnRenameRequested;
    std::unordered_map<std::uint64_t, bool> m_ExpandedStates;
    EntityHandle m_RenameHandle = InvalidEntityHandle;
    bool m_SynchronizingSelection = false;
};

} // namespace Raven
