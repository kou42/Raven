#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/SoftBody/Debug/SoftBodyParticleTriangleCandidateDebugSnapshot.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// SoftBodyParticleTrianglePairInspection
// ============================================================================
// Browser Debug Viewerで1つのParticle × Triangle Pairを選択した際に、Reject判定を数値で確認するための
// 値型Snapshotです。Simulationの判定結果には使用せず、Debug表示専用です。
struct SoftBodyParticleTrianglePairInspection
{
    uint32_t ParticleIndex = 0u;
    uint32_t TriangleIndex = 0u;
    SoftBodyParticleTriangleCandidateDebugReason Reason =
        SoftBodyParticleTriangleCandidateDebugReason::AABBReject;

    float Thickness = 0.0f;

    // Thicknessで膨張済みTriangle AABBから各軸方向へ外れている距離です。
    // AABB内なら各成分0、AABB Rejectなら少なくとも1成分が正になります。
    math::Vec3 AABBOutsideDistance{};

    // Triangle Planeへの符号付きWorld Space距離です。
    // Runtimeでは非正規化法線の平方比較を使いますが、Inspectorでは読みやすさを優先して正規化します。
    float PlaneSignedDistance = 0.0f;
    bool PlaneValid = false;

    // AB / BC / CA Edge Half-Spaceへの符号付きWorld Space距離です。
    // 負値はTriangle外側を表します。絶対値がThicknessを超えた負値なら、そのEdgeでRejectされます。
    float EdgeABSignedDistance = 0.0f;
    float EdgeBCSignedDistance = 0.0f;
    float EdgeCASignedDistance = 0.0f;
    bool EdgeHalfSpaceValid = false;

    bool Valid = false;
};

// ============================================================================
// SoftBodyParticleTrianglePairInspector
// ============================================================================
// Candidate Snapshot内に実在するPairだけをInspectionへ変換します。
// これにより「Spatial Hash Cell Candidateではない任意のParticle/Triangle」を誤ってReject Pairとして
// 表示することを防ぎます。
class SoftBodyParticleTrianglePairInspector
{
public:
    static bool Inspect(
        const std::vector<SoftBodyParticle>& particles,
        const std::vector<uint32_t>& triangleIndices,
        const SoftBodyParticleTriangleCandidateDebugSnapshot& snapshot,
        uint32_t particleIndex,
        uint32_t triangleIndex,
        SoftBodyParticleTrianglePairInspection& outInspection);
};

} // namespace ph
} // namespace Raven
