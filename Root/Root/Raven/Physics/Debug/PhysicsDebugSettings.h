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
    // デフォルトはON。実行時にHキーでOverlayの表示/非表示を切り替えます。
    // 将来の2D OverlayでSolver Statisticsを表示するための設定です。
    bool ShowSolverStatistics = true;

    // 実行時にCキーで切り替えます。
    // Narrow Phaseで生成された接触点を十字マーカーで表示します。
    bool ShowContactPoints = false;

    // 実行時にNキーで切り替えます。
    // Contact PointからManifold Normal方向へ線を表示します。
    bool ShowContactNormals = false;

    // 実行時にBキーで切り替えます。
    // Colliderの現在Transformから計算したtight AABBを表示します。
    bool ShowAABB = false;

    // 実行時にFキーで切り替えます。
    // PhysicsWorldが実際に使用しているDynamic TreeのLeaf Fat AABBを表示します。
    bool ShowFatAABB = false;

    // 実行時にTキーで切り替えます。
    // PhysicsWorldが実際に使用しているDynamic TreeのBranch AABBを表示します。
    bool ShowDynamicAABBTree = false;

    // 実行時にPキーで切り替えます。
    // Broad Phase候補Pairの中心間を線で表示します。
    bool ShowBroadPhasePairs = false;

    // 単位はワールド空間。接触法線の線分長と、接触点マーカー半径を表します。
    // Contact表示のワールド空間サイズです。
    float ContactNormalLength = 0.5f;
    float ContactPointRadius = 0.03f;
};

} // namespace Raven::ph
