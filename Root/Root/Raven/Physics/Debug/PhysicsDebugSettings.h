#pragma once

namespace Raven::ph
{

// ============================================================================
// PhysicsDebugSettings
// ============================================================================
// PhysicsDebugRendererと将来のDebug UIが共有する表示設定です。
//
// 重要:
// PhysicsWorldはシミュレーションと診断データの提供だけを担当し、
// 描画のON/OFF状態はScene/Debug側で管理します。
struct PhysicsDebugSettings
{
    // 将来の2D OverlayでSolver Statisticsを表示するための設定です。
    bool ShowSolverStatistics = true;

    // Narrow Phaseで生成された接触点を十字マーカーで表示します。
    bool ShowContactPoints = false;

    // Contact PointからManifold Normal方向へ線を表示します。
    bool ShowContactNormals = false;

    // Colliderの現在Transformから計算したtight AABBを表示します。
    bool ShowAABB = false;

    // PhysicsWorldが実際に使用しているDynamic TreeのLeaf Fat AABBを表示します。
    bool ShowFatAABB = false;

    // PhysicsWorldが実際に使用しているDynamic TreeのBranch AABBを表示します。
    bool ShowDynamicAABBTree = false;

    // Broad Phase候補Pairの中心間を線で表示します。
    bool ShowBroadPhasePairs = false;

    float ContactNormalLength = 0.5f;
    float ContactPointRadius = 0.03f;
};

} // namespace Raven::ph
