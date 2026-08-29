#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Particle-Triangle Candidate Debug Reject Reason
// ============================================================================
// GenerateParticleTriangleCandidates() のcheap reject funnelをBrowser Debug Viewerなどから
// 追跡するための分類です。通常Solverの候補生成順と同じ順序で最初に成立したReject理由を記録します。
//
// AABBReject:
//   同じSpatial Hash Cellから取得されたものの、ParticleがThicknessで膨張したTriangle AABB外です。
//
// TopologyReject:
//   Particle自身がTriangleの構成頂点です。自己Triangleなので自己衝突候補から除外されます。
//
// PlaneReject:
//   Triangleを含む無限平面までの距離がThicknessより大きく、接触できません。
//
// EdgeReject:
//   Plane付近には存在しますが、TriangleのEdge Half-Space外へThicknessより遠く離れています。
//
// NarrowPhaseCandidate:
//   すべてのcheap rejectを通過し、通常SolverではClosest Point計算へ送られる候補です。
enum class SoftBodyParticleTriangleCandidateDebugReason : uint8_t
{
    AABBReject = 0u,
    TopologyReject,
    PlaneReject,
    EdgeReject,
    NarrowPhaseCandidate
};

// 1つの「Particle × Triangle Cell Candidate」がどの段階で脱落したかを表します。
// ParticleIndex / TriangleIndexを保持するため、SVG側ではParticleだけでなく対象Triangleも強調できます。
//
// 以前はInspector向けのAABB / Plane / Edge距離も各Recordへ保持していましたが、全Candidateへ同じ種類の
// 診断値を保存するとSnapshotメモリが大きくなります。現在はRecordを「Pair識別 + Reject理由」に限定し、
// 詳細距離はParticle/Triangleの両方が選択されたときだけSoftBodyParticleTrianglePairInspectorが再計算します。
// Runtimeのcheap reject自体は非正規化法線 + 平方比較のままなので、このDebug設計変更はSimulation hot pathへ
// 影響しません。
struct SoftBodyParticleTriangleCandidateDebugInfo
{
    uint32_t ParticleIndex = 0u;
    uint32_t TriangleIndex = 0u;
    SoftBodyParticleTriangleCandidateDebugReason Reason =
        SoftBodyParticleTriangleCandidateDebugReason::AABBReject;
};

// Snapshot全体の件数です。
// Recordsを毎回集計し直さなくてもViewer上部へFunnelを表示できるよう、生成時に同時集計します。
struct SoftBodyParticleTriangleCandidateDebugStatistics
{
    uint64_t CellCandidateCount = 0u;
    uint64_t AABBRejectCount = 0u;
    uint64_t TopologyRejectCount = 0u;
    uint64_t PlaneRejectCount = 0u;
    uint64_t EdgeRejectCount = 0u;
    uint64_t NarrowPhaseCandidateCount = 0u;
};

struct SoftBodyParticleTriangleCandidateDebugSnapshot
{
    SoftBodyParticleTriangleCandidateDebugStatistics Statistics{};
    std::vector<SoftBodyParticleTriangleCandidateDebugInfo> Records;

    // Pair InspectorでRuntime判定値と同じ閾値を表示するため、Build時のThicknessをSnapshotへ保存します。
    // 各Recordへ同じfloatを重複保持せずSnapshot単位にすることで、診断用メモリ増加を抑えます。
    // Inspectorは選択Pairを再計算するとき、この値をAABB expansion / Plane / Edge判定の共通閾値として使用します。
    float Thickness = 0.0f;

    void Clear()
    {
        Statistics = {};
        Records.clear();
        Thickness = 0.0f;
    }
};

// ============================================================================
// SoftBodyParticleTriangleCandidateDebugSnapshotBuilder
// ============================================================================
// Runtime Broad PhaseへDebug用vector push_backを追加すると、最適化対象そのもののallocation / cache挙動を
// 変えてしまいます。そのためBrowser更新時など低頻度のタイミングで、現在Particle/Topologyから
// Spatial Hash登録とcheap rejectを再評価してSnapshotを構築します。
//
// 判定式と順序はSoftBodyTriangleSpatialHashGrid::GenerateParticleTriangleCandidates()に合わせています。
// このクラスは診断専用であり、Simulation結果の決定には使用しません。
class SoftBodyParticleTriangleCandidateDebugSnapshotBuilder
{
public:
    static void Build(
        const std::vector<SoftBodyParticle>& particles,
        const std::vector<SoftBodyTriangle>& triangles,
        float spatialHashCellSize,
        float thickness,
        SoftBodyParticleTriangleCandidateDebugSnapshot& outSnapshot);
};

} // namespace ph
} // namespace Raven
