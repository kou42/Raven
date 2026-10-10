#include "Raven/Editor/EditorDockLayout.h"

#include <filesystem>
#include <utility>

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
        { kHierarchyLeafId, { DockTabId(EditorDockTabId::SceneHierarchy), "Scene Hierarchy", true } },
        { kViewportLeafId, { DockTabId(EditorDockTabId::SceneView), "Scene View", true } },
        { kViewportLeafId, { DockTabId(EditorDockTabId::GameView), "Game View", true } },
        { kDiagnosticsLeafId, { DockTabId(EditorDockTabId::Statistics), "Statistics", true } },
        { kDiagnosticsLeafId, { DockTabId(EditorDockTabId::AnimationDebug), "Animation Debug", true } },
        { kInspectorLeafId, { DockTabId(EditorDockTabId::Inspector), "Inspector", true } }
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
