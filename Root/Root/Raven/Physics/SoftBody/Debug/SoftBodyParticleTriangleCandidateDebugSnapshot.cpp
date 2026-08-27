#include "Raven/Physics/SoftBody/Debug/SoftBodyParticleTriangleCandidateDebugSnapshot.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumDebugCellSize = 1.0e-4f;

struct DebugCellCoord
{
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Z = 0;

    bool operator<(const DebugCellCoord& rhs) const
    {
        return std::tie(X, Y, Z) < std::tie(rhs.X, rhs.Y, rhs.Z);
    }
};

struct DebugTriangleCache
{
    math::Vec3 Minimum{};
    math::Vec3 Maximum{};

    uint32_t ParticleA = 0u;
    uint32_t ParticleB = 0u;
    uint32_t ParticleC = 0u;

    math::Vec3 PlaneNormal{};
    float PlaneOffset = 0.0f;
    float PlaneDistanceThresholdSq = 0.0f;
    bool PlaneValid = false;

    math::Vec3 EdgeABNormal{};
    math::Vec3 EdgeBCNormal{};
    math::Vec3 EdgeCANormal{};
    float EdgeABOffset = 0.0f;
    float EdgeBCOffset = 0.0f;
    float EdgeCAOffset = 0.0f;
    float EdgeABDistanceThresholdSq = 0.0f;
    float EdgeBCDistanceThresholdSq = 0.0f;
    float EdgeCADistanceThresholdSq = 0.0f;
    bool EdgeHalfSpaceValid = false;

    bool Valid = false;
};

DebugCellCoord ComputeDebugCellCoord(const math::Vec3& position, float inverseCellSize)
{
    DebugCellCoord cell{};
    cell.X = static_cast<int32_t>(std::floor(position.x * inverseCellSize));
    cell.Y = static_cast<int32_t>(std::floor(position.y * inverseCellSize));
    cell.Z = static_cast<int32_t>(std::floor(position.z * inverseCellSize));
    return cell;
}

void AppendRecord(
    SoftBodyParticleTriangleCandidateDebugSnapshot& snapshot,
    uint32_t particleIndex,
    uint32_t triangleIndex,
    SoftBodyParticleTriangleCandidateDebugReason reason)
{
    SoftBodyParticleTriangleCandidateDebugInfo info{};
    info.ParticleIndex = particleIndex;
    info.TriangleIndex = triangleIndex;
    info.Reason = reason;
    snapshot.Records.push_back(info);

    switch (reason)
    {
    case SoftBodyParticleTriangleCandidateDebugReason::AABBReject:
        ++snapshot.Statistics.AABBRejectCount;
        break;
    case SoftBodyParticleTriangleCandidateDebugReason::TopologyReject:
        ++snapshot.Statistics.TopologyRejectCount;
        break;
    case SoftBodyParticleTriangleCandidateDebugReason::PlaneReject:
        ++snapshot.Statistics.PlaneRejectCount;
        break;
    case SoftBodyParticleTriangleCandidateDebugReason::EdgeReject:
        ++snapshot.Statistics.EdgeRejectCount;
        break;
    case SoftBodyParticleTriangleCandidateDebugReason::NarrowPhaseCandidate:
        ++snapshot.Statistics.NarrowPhaseCandidateCount;
        break;
    }
}
} // namespace

void SoftBodyParticleTriangleCandidateDebugSnapshotBuilder::Build(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangle>& triangles,
    float spatialHashCellSize,
    float thickness,
    SoftBodyParticleTriangleCandidateDebugSnapshot& outSnapshot)
{
    outSnapshot.Clear();

    if (particles.empty() == true || triangles.empty() == true)
    {
        return;
    }

    // ========================================================================
    // Debug-only Broad Phase Reconstruction
    // ========================================================================
    // 通常Gridのprivate BucketをViewerへ公開せず、現在位置から同じCell登録規則を再構築します。
    // std::map / std::vectorを使うのは意図的です。この処理は100ms程度のBrowser Snapshot更新時だけ
    // 実行する診断経路であり、Simulation hot pathのFlat Hash性能には影響させません。
    const float safeCellSize = std::max(spatialHashCellSize, MinimumDebugCellSize);
    const float inverseCellSize = 1.0f / safeCellSize;
    const float safeThickness = std::max(thickness, 0.0f);
    const float thicknessSq = safeThickness * safeThickness;

    std::vector<DebugTriangleCache> triangleCaches(triangles.size());
    std::map<DebugCellCoord, std::vector<uint32_t>> cellTriangles;

    for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
    {
        const SoftBodyTriangle& triangle = triangles[triangleIndex];
        if (triangle.ParticleA >= particles.size()
            || triangle.ParticleB >= particles.size()
            || triangle.ParticleC >= particles.size())
        {
            continue;
        }

        DebugTriangleCache& cache = triangleCaches[triangleIndex];
        cache.ParticleA = triangle.ParticleA;
        cache.ParticleB = triangle.ParticleB;
        cache.ParticleC = triangle.ParticleC;

        const math::Vec3& a = particles[triangle.ParticleA].Position;
        const math::Vec3& b = particles[triangle.ParticleB].Position;
        const math::Vec3& c = particles[triangle.ParticleC].Position;

        cache.Minimum.x = std::min({ a.x, b.x, c.x }) - safeThickness;
        cache.Minimum.y = std::min({ a.y, b.y, c.y }) - safeThickness;
        cache.Minimum.z = std::min({ a.z, b.z, c.z }) - safeThickness;
        cache.Maximum.x = std::max({ a.x, b.x, c.x }) + safeThickness;
        cache.Maximum.y = std::max({ a.y, b.y, c.y }) + safeThickness;
        cache.Maximum.z = std::max({ a.z, b.z, c.z }) + safeThickness;
        cache.Valid = true;

        // ====================================================================
        // Plane / Edge Cache
        // ====================================================================
        // Runtime Gridと同様に非正規化法線を保持し、Candidate判定ではsqrt / Normalizeを使わず
        // Thicknessとの平方比較だけでReject理由を決定します。
        const math::Vec3 edgeABVector = b - a;
        const math::Vec3 edgeACVector = c - a;
        const math::Vec3 planeNormal = math::Vec3::Cross(edgeABVector, edgeACVector);
        const float planeNormalLengthSq = planeNormal.LengthSq();

        if (planeNormalLengthSq > math::Epsilon * math::Epsilon)
        {
            cache.PlaneNormal = planeNormal;
            cache.PlaneOffset = math::Vec3::Dot(planeNormal, a);
            cache.PlaneDistanceThresholdSq = thicknessSq * planeNormalLengthSq;
            cache.PlaneValid = true;

            const math::Vec3 edgeBCVector = c - b;
            const math::Vec3 edgeCAVector = a - c;
            const math::Vec3 edgeABNormal = math::Vec3::Cross(planeNormal, edgeABVector);
            const math::Vec3 edgeBCNormal = math::Vec3::Cross(planeNormal, edgeBCVector);
            const math::Vec3 edgeCANormal = math::Vec3::Cross(planeNormal, edgeCAVector);

            const float edgeABNormalLengthSq = edgeABNormal.LengthSq();
            const float edgeBCNormalLengthSq = edgeBCNormal.LengthSq();
            const float edgeCANormalLengthSq = edgeCANormal.LengthSq();

            if (edgeABNormalLengthSq > math::Epsilon * math::Epsilon
                && edgeBCNormalLengthSq > math::Epsilon * math::Epsilon
                && edgeCANormalLengthSq > math::Epsilon * math::Epsilon)
            {
                cache.EdgeABNormal = edgeABNormal;
                cache.EdgeBCNormal = edgeBCNormal;
                cache.EdgeCANormal = edgeCANormal;
                cache.EdgeABOffset = math::Vec3::Dot(edgeABNormal, a);
                cache.EdgeBCOffset = math::Vec3::Dot(edgeBCNormal, b);
                cache.EdgeCAOffset = math::Vec3::Dot(edgeCANormal, c);
                cache.EdgeABDistanceThresholdSq = thicknessSq * edgeABNormalLengthSq;
                cache.EdgeBCDistanceThresholdSq = thicknessSq * edgeBCNormalLengthSq;
                cache.EdgeCADistanceThresholdSq = thicknessSq * edgeCANormalLengthSq;
                cache.EdgeHalfSpaceValid = true;
            }
        }

        const DebugCellCoord minimumCell = ComputeDebugCellCoord(cache.Minimum, inverseCellSize);
        const DebugCellCoord maximumCell = ComputeDebugCellCoord(cache.Maximum, inverseCellSize);

        for (int32_t z = minimumCell.Z; z <= maximumCell.Z; ++z)
        {
            for (int32_t y = minimumCell.Y; y <= maximumCell.Y; ++y)
            {
                for (int32_t x = minimumCell.X; x <= maximumCell.X; ++x)
                {
                    DebugCellCoord cell{};
                    cell.X = x;
                    cell.Y = y;
                    cell.Z = z;
                    cellTriangles[cell].push_back(static_cast<uint32_t>(triangleIndex));
                }
            }
        }
    }

    // ========================================================================
    // Candidate Funnel Classification
    // ========================================================================
    // 判定順序はRuntimeのGenerateParticleTriangleCandidates()と一致させます。
    // 「複数条件に該当する候補」は最初に成立した理由だけを記録するため、Viewerの色とProfilerの
    // Funnel Counterを同じ意味で比較できます。
    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const uint32_t particleIndex32 = static_cast<uint32_t>(particleIndex);
        const math::Vec3& particlePosition = particles[particleIndex].Position;
        const DebugCellCoord particleCell = ComputeDebugCellCoord(particlePosition, inverseCellSize);

        const auto cellIterator = cellTriangles.find(particleCell);
        if (cellIterator == cellTriangles.end())
        {
            continue;
        }

        for (uint32_t triangleIndex : cellIterator->second)
        {
            ++outSnapshot.Statistics.CellCandidateCount;

            if (triangleIndex >= triangleCaches.size())
            {
                AppendRecord(
                    outSnapshot,
                    particleIndex32,
                    triangleIndex,
                    SoftBodyParticleTriangleCandidateDebugReason::AABBReject);
                continue;
            }

            const DebugTriangleCache& cache = triangleCaches[triangleIndex];
            if (cache.Valid == false
                || particlePosition.x < cache.Minimum.x
                || particlePosition.x > cache.Maximum.x
                || particlePosition.y < cache.Minimum.y
                || particlePosition.y > cache.Maximum.y
                || particlePosition.z < cache.Minimum.z
                || particlePosition.z > cache.Maximum.z)
            {
                AppendRecord(
                    outSnapshot,
                    particleIndex32,
                    triangleIndex,
                    SoftBodyParticleTriangleCandidateDebugReason::AABBReject);
                continue;
            }

            if (cache.ParticleA == particleIndex32
                || cache.ParticleB == particleIndex32
                || cache.ParticleC == particleIndex32)
            {
                AppendRecord(
                    outSnapshot,
                    particleIndex32,
                    triangleIndex,
                    SoftBodyParticleTriangleCandidateDebugReason::TopologyReject);
                continue;
            }

            if (cache.PlaneValid == true)
            {
                const float signedScaledDistance =
                    math::Vec3::Dot(cache.PlaneNormal, particlePosition) - cache.PlaneOffset;
                const float signedScaledDistanceSq = signedScaledDistance * signedScaledDistance;

                if (signedScaledDistanceSq > cache.PlaneDistanceThresholdSq)
                {
                    AppendRecord(
                        outSnapshot,
                        particleIndex32,
                        triangleIndex,
                        SoftBodyParticleTriangleCandidateDebugReason::PlaneReject);
                    continue;
                }
            }

            if (cache.EdgeHalfSpaceValid == true)
            {
                bool edgeRejected = false;

                const float edgeABSignedScaled =
                    math::Vec3::Dot(cache.EdgeABNormal, particlePosition) - cache.EdgeABOffset;
                if (edgeABSignedScaled < 0.0f
                    && edgeABSignedScaled * edgeABSignedScaled > cache.EdgeABDistanceThresholdSq)
                {
                    edgeRejected = true;
                }

                if (edgeRejected == false)
                {
                    const float edgeBCSignedScaled =
                        math::Vec3::Dot(cache.EdgeBCNormal, particlePosition) - cache.EdgeBCOffset;
                    if (edgeBCSignedScaled < 0.0f
                        && edgeBCSignedScaled * edgeBCSignedScaled > cache.EdgeBCDistanceThresholdSq)
                    {
                        edgeRejected = true;
                    }
                }

                if (edgeRejected == false)
                {
                    const float edgeCASignedScaled =
                        math::Vec3::Dot(cache.EdgeCANormal, particlePosition) - cache.EdgeCAOffset;
                    if (edgeCASignedScaled < 0.0f
                        && edgeCASignedScaled * edgeCASignedScaled > cache.EdgeCADistanceThresholdSq)
                    {
                        edgeRejected = true;
                    }
                }

                if (edgeRejected == true)
                {
                    AppendRecord(
                        outSnapshot,
                        particleIndex32,
                        triangleIndex,
                        SoftBodyParticleTriangleCandidateDebugReason::EdgeReject);
                    continue;
                }
            }

            AppendRecord(
                outSnapshot,
                particleIndex32,
                triangleIndex,
                SoftBodyParticleTriangleCandidateDebugReason::NarrowPhaseCandidate);
        }
    }
}

} // namespace ph
} // namespace Raven
