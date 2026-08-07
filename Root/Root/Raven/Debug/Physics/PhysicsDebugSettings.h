#pragma once

namespace Raven::ph
{

// ============================================================================
// PhysicsDebugSettings
// ============================================================================
// Physicsの診断表示に関するON/OFFを1か所にまとめます。
//
// 重要:
// PhysicsWorld自身には描画責務を持たせません。
// PhysicsWorldはシミュレーションと診断データの提供だけを担当し、
// この設定値はPhysicsDebugRendererや将来のDebug UIから参照します。
struct PhysicsDebugSettings
{
    // Solver Statisticsを2D Debug UIへ表示するための設定です。
    // UI基盤導入前でも設定構造だけ先に用意しておきます。
    bool ShowSolverStatistics = true;

    // Narrow Phaseで生成された接触点を表示します。
    bool ShowContactPoints = false;

    // 各接触点からContactManifold::Normal方向へ法線を表示します。
    bool ShowContactNormals = false;

    // Colliderの現在位置から計算したtight AABBを表示します。
    bool ShowAABB = false;

    // Dynamic AABB TreeのLeafが保持するFat AABBを表示します。
    bool ShowFatAABB = false;

    // LeafだけでなくBranchを含め、Dynamic AABB Tree全体のBoundsを表示します。
    bool ShowDynamicAABBTree = false;

    // Contact Normalをワールド上で何m表示するかを指定します。
    float ContactNormalLength = 0.5f;

    // Contact Pointの十字マーカーの半径です。
    // 現段階ではDebugRendererをline中心に構築するため、球Meshではなく
    // X/Y/Z方向の短い線分で接触点を可視化します。
    float ContactPointRadius = 0.03f;
};

} // namespace Raven::ph
