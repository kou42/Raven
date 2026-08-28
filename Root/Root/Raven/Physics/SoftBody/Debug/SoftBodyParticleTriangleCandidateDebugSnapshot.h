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
// Inspector用の距離は可能な限りWorld Space距離へ正規化して保存します。
// Runtime側のcheap reject自体はsqrtを避けるため非正規化法線 + 平方比較ですが、Debug Snapshotは低頻度なので
// 人間が読みやすい距離へ変換してもSimulation hot pathには影響しません。
struct SoftBodyParticleTriangleCandidateDebugInfo
{
    uint32_t ParticleIndex = 0u;
    uint32_t TriangleIndex = 0u;
    SoftBodyParticleTriangleCandidateDebugReason Reason =
        SoftBodyParticleTriangleCandidateDebugReason::AABBReject;

    float Thickness = 0.0f;

    // Expanded Triangle AABBから各軸方向へどれだけ外れているかです。
    // AABB内なら0、AABB Rejectなら少なくとも1軸が正になります。
    math::Vec3 AABBOutsideDistance{};

    // Triangle Planeへの符号付きWorld Space距離です。
    // Planeが縮退している場合はPlaneValid=falseになります。
    float PlaneSignedDistance = 0.0f;
    bool PlaneValid = false;

    // AB / BC / CA Edge Half-Spaceの符号付きWorld Space距離です。
    // 負値はTriangle外側を意味し、abs(distance) > ThicknessならそのEdgeでRejectされます。
    float EdgeABSignedDistance = 0.0f;
    float EdgeBCSignedDistance = 0.0f;
    float EdgeCASignedDistance = 0.0f;
    bool EdgeHalfSpaceValid = false;
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

    void Clear()
    {
        Statistics = {};
        Records.clear();
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
