#include "Raven/Editor/EditorDockLayout.h"

#include <filesystem>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Raven
{
namespace
{

constexpr std::uint64_t kRootSplitId = 1u;
constexpr std::uint64_t kHierarchyLeafId = 2u;
constexpr std::uint64_t kWorkspaceSplitId = 3u;
constexpr std::uint64_t kCenterSplitId = 4u;
constexpr std::uint64_t kViewportLeafId = 5u;
constexpr std::uint64_t kDiagnosticsLeafId = 6u;
constexpr std::uint64_t kInspectorLeafId = 7u;

std::uint64_t DockTabId(EditorDockTabId id)
{
    return static_cast<std::uint64_t>(id);
}

} // namespace

UIDockSpaceSnapshot CreateDefaultEditorDockLayout()
{
    UIDockSpaceSnapshot snapshot;
    // PreorderでRoot -> First -> Secondを記録します。Horizontalは左右、Verticalは上下です。
    snapshot.Structure =
    {
        { kRootSplitId, UIDockNodeKind::Split, UIDockSplitAxis::Horizontal, 0.20f, 0u },
        { kHierarchyLeafId, UIDockNodeKind::Tabs, UIDockSplitAxis::Horizontal, 0.5f, 1u },
        { kWorkspaceSplitId, UIDockNodeKind::Split, UIDockSplitAxis::Horizontal, 0.74f, 1u },
        { kCenterSplitId, UIDockNodeKind::Split, UIDockSplitAxis::Vertical, 0.72f, 2u },
        { kViewportLeafId, UIDockNodeKind::Tabs, UIDockSplitAxis::Horizontal, 0.5f, 3u },
        { kDiagnosticsLeafId, UIDockNodeKind::Tabs, UIDockSplitAxis::Horizontal, 0.5f, 3u },
        { kInspectorLeafId, UIDockNodeKind::Tabs, UIDockSplitAxis::Horizontal, 0.5f, 2u }
    };
    snapshot.Tabs =
    {
        { kHierarchyLeafId, { DockTabId(EditorDockTabId::SceneHierarchy), "Scene Hierarchy", false } },
        { kViewportLeafId, { DockTabId(EditorDockTabId::SceneView), "Scene View", false } },
        { kViewportLeafId, { DockTabId(EditorDockTabId::GameView), "Game View", false } },
        { kDiagnosticsLeafId, { DockTabId(EditorDockTabId::Statistics), "Statistics", false } },
        { kDiagnosticsLeafId, { DockTabId(EditorDockTabId::AnimationDebug), "Animation Debug", false } },
        { kInspectorLeafId, { DockTabId(EditorDockTabId::Inspector), "Inspector", false } }
    };
    snapshot.Selections =
    {
        { kHierarchyLeafId, DockTabId(EditorDockTabId::SceneHierarchy) },
        { kViewportLeafId, DockTabId(EditorDockTabId::SceneView) },
        { kDiagnosticsLeafId, DockTabId(EditorDockTabId::Statistics) },
        { kInspectorLeafId, DockTabId(EditorDockTabId::Inspector) }
    };
    return snapshot;
}

bool BuildEditorDockLayoutForSave(const UIDockSpaceSnapshot& current,
    const std::vector<UIDockTabRestoreRecord>& detached,
    UIDockSpaceSnapshot& outSnapshot, std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    std::string validationText;
    if (SerializeDockSnapshot(current, validationText, errorMessage) == false)
    {
        return false;
    }
    UIDockSpaceSnapshot result = current;
    std::unordered_map<std::uint64_t, std::vector<UITabItem>> tabs;
    std::unordered_set<std::uint64_t> existingIds;
    for (const auto& node : current.Structure)
    {
        if (node.Kind == UIDockNodeKind::Tabs)
        {
            tabs.emplace(node.Id, std::vector<UITabItem>{});
        }
    }
    if (tabs.empty() == true)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "保存先のTab Paneが存在しません";
        }
        return false;
    }
    for (const auto& record : current.Tabs)
    {
        if (record.Tab.Id < DockTabId(EditorDockTabId::SceneView) ||
            record.Tab.Id > DockTabId(EditorDockTabId::AnimationDebug) ||
            existingIds.insert(record.Tab.Id).second == false)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Editorの保存対象Tab IDが不正または重複しています";
            }
            return false;
        }
        tabs.at(record.LeafId).push_back(record.Tab);
    }
    // 同じPaneから複数Tabを順次抽出するとIndexが詰まります。抽出の逆順で挿入すれば、
    // 保存時に残っているTabの相対順序を変えず、元のIndexを再現できます。
    auto ordered = detached;
    std::sort(ordered.begin(), ordered.end(), [](const auto& first, const auto& second)
    {
        return first.DetachOrder != second.DetachOrder
            ? first.DetachOrder > second.DetachOrder
            : first.Record.Tab.Id > second.Record.Tab.Id;
    });
    const UIDockSpaceSnapshot defaults = CreateDefaultEditorDockLayout();
    for (const auto& entry : ordered)
    {
        const auto& tab = entry.Record.Tab;
        if (tab.Id < DockTabId(EditorDockTabId::SceneView) ||
            tab.Id > DockTabId(EditorDockTabId::AnimationDebug))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Editorの保存対象ではない切り離しTab IDです";
            }
            return false;
        }
        // 既にClose復帰したTabや重複通知では現在のDockを優先し、二重登録しません。
        if (existingIds.insert(tab.Id).second == false)
        {
            continue;
        }
        std::uint64_t leaf = entry.Record.LeafId;
        if (tabs.find(leaf) == tabs.end())
        {
            // 元Paneを畳んだ場合は既定Pane IDが生存していればそこへ、なければ
            // 現在のTreeで最初のTab Paneへ戻します。保存時にSplit構成を作り直しません。
            const auto standard = std::find_if(defaults.Tabs.begin(), defaults.Tabs.end(),
                [&](const UIDockTabRecord& value) { return value.Tab.Id == tab.Id; });
            if (standard != defaults.Tabs.end() && tabs.find(standard->LeafId) != tabs.end())
            {
                leaf = standard->LeafId;
            }
            else
            {
                const auto first = std::find_if(current.Structure.begin(), current.Structure.end(),
                    [](const UIDockLayoutRecord& value) { return value.Kind == UIDockNodeKind::Tabs; });
                leaf = first->Id; // currentは既存Serializerで検証済みなのでTab Paneが存在します。
            }
        }
        auto& items = tabs.at(leaf);
        items.insert(items.begin() + std::min(entry.TabIndex, items.size()), tab);
        auto selection = std::find_if(result.Selections.begin(), result.Selections.end(),
            [leaf](const auto& value) { return value.first == leaf; });
        const auto liveSelection = std::find_if(current.Selections.begin(), current.Selections.end(),
            [leaf](const auto& value) { return value.first == leaf; });
        // 現在のDockに選択が残っていれば優先します。空Paneだけは逆順の復帰情報から
        // 選択を戻し、全Tabを切り離した場合も有効な選択を一つ確保します。
        if (liveSelection->second == 0u)
        {
            const auto selected = std::find_if(items.begin(), items.end(),
                [&](const UITabItem& value) { return value.Id == entry.SelectedTabId; });
            if (selected != items.end())
            {
                selection->second = selected->Id;
            }
            else if (selection->second == 0u)
            {
                selection->second = items.front().Id;
            }
        }
    }
    result.Tabs.clear();
    for (const auto& node : current.Structure)
    {
        if (node.Kind == UIDockNodeKind::Tabs)
        {
            for (const auto& tab : tabs.at(node.Id))
            {
                result.Tabs.push_back({ node.Id, tab });
            }
        }
    }
    // 検証失敗で既存保存ファイルや呼び出し側のSnapshotを部分更新しないよう、
    // 既存の形式検証を再利用し、成功時だけ結果を公開します。
    if (SerializeDockSnapshot(result, validationText, errorMessage) == false)
    {
        return false;
    }
    outSnapshot = std::move(result);
    return true;
}

EditorDockLayoutLoadResult LoadEditorDockLayoutOrDefault(
    const std::string& filePath,
    UIDockSpaceSnapshot& outSnapshot)
{
    EditorDockLayoutLoadResult result;
    const std::filesystem::path primary(filePath);
    std::filesystem::path backup = primary;
    backup += ".bak";
    std::error_code primaryStatusError;
    std::error_code backupStatusError;
    const bool primaryExists = filePath.empty() == false &&
        std::filesystem::exists(primary, primaryStatusError) == true;
    const bool backupExists = filePath.empty() == false &&
        std::filesystem::exists(backup, backupStatusError) == true;

    UIDockSpaceSnapshot loaded;
    std::string primaryError;
    if (filePath.empty() == false &&
        LoadDockSnapshot(filePath, loaded, &primaryError) == true)
    {
        outSnapshot = std::move(loaded);
        result.Source = primaryExists == true ?
            EditorDockLayoutSource::SavedSnapshot :
            EditorDockLayoutSource::BackupSnapshot;
        return result;
    }

    // Primaryが存在する破損ケースでは汎用LoaderがBackupを試さないため、明示的に再試行します。
    std::string backupError;
    if (primaryExists == true && backupExists == true &&
        LoadDockSnapshot(backup.string(), loaded, &backupError) == true)
    {
        outSnapshot = std::move(loaded);
        result.Source = EditorDockLayoutSource::BackupSnapshot;
        result.Diagnostic = std::move(primaryError);
        return result;
    }

    outSnapshot = CreateDefaultEditorDockLayout();
    result.Source = primaryExists == false && backupExists == false &&
        primaryStatusError.value() == 0 && backupStatusError.value() == 0 &&
        filePath.empty() == false ?
        EditorDockLayoutSource::DefaultFirstRun :
        EditorDockLayoutSource::DefaultRecovery;
    result.Diagnostic = primaryError;
    if (backupError.empty() == false)
    {
        if (result.Diagnostic.empty() == false)
        {
            result.Diagnostic += " / ";
        }
        result.Diagnostic += backupError;
    }
    if (primaryStatusError.value() != 0 || backupStatusError.value() != 0)
    {
        if (result.Diagnostic.empty() == false)
        {
            result.Diagnostic += " / ";
        }
        result.Diagnostic += "Dock SnapshotのFile状態を確認できません";
    }
    return result;
}

} // namespace Raven
