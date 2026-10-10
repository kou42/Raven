#pragma once

#include "Raven/UI/Docking/UIDockSpace.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Raven
{

// imgui.iniとは別のRaven UI専用保存先です。
inline constexpr std::string_view kEditorDockLayoutFileName =
    "RavenEditorDock.layout.json";

// Content FactoryがEditor Panelを再生成するための安定IDです。
enum class EditorDockTabId : std::uint64_t
{
    SceneView = 1001u,
    GameView = 1002u,
    SceneHierarchy = 1003u,
    Inspector = 1004u,
    Statistics = 1005u,
    AnimationDebug = 1006u
};

enum class EditorDockLayoutSource
{
    SavedSnapshot = 0,
    BackupSnapshot,
    DefaultFirstRun,
    DefaultRecovery
};

struct EditorDockLayoutLoadResult
{
    EditorDockLayoutSource Source = EditorDockLayoutSource::DefaultFirstRun;
    // Saved/Backup成功時は空です。Fallback時は採用しなかった保存状態の理由を保持します。
    std::string Diagnostic;
};

// 左Hierarchy、中央Scene/Gameと診断Panel、右Inspectorの既定配置を返します。
UIDockSpaceSnapshot CreateDefaultEditorDockLayout();

// 保存済みSnapshot、Backup、既定配置の順で採用します。
// outSnapshotは必ず検証済みSnapshotへ更新され、旧imgui.iniは読み込みません。
EditorDockLayoutLoadResult LoadEditorDockLayoutOrDefault(
    const std::string& filePath,
    UIDockSpaceSnapshot& outSnapshot);

} // namespace Raven
