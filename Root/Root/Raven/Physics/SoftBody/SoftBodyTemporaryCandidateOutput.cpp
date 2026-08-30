#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

#include <array>
#include <cstdint>

#include "Raven/Core/CPUProfiler.h"

namespace Raven
{
namespace ph
{
namespace
{

// ============================================================================
// Temporary-allocation measured Candidate Output
// ============================================================================
// Phase ②ではCandidate vectorそのもののHeap allocationを計測対象に含めます。
// 通常vectorへ一度生成してからCounter付きvectorへcopyすると、元vectorのallocationが
// Counterから漏れてしまうため、Spatial Hashから計測vectorへ直接push_backします。
//
// ここでは既存Candidate生成アルゴリズムと判定順を変更しません。
// Phase ③でFrameAllocatorへ切り替えた際にも、候補集合やNarrow Phaseの仕事量を変えず、
// backing allocatorだけの差としてBefore / After比較できることを優先します。
struct TemporaryNeighborOffset
{
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Z = 0;
};

constexpr std::array<TemporaryNeighborOffset, 13u> TemporaryUniqueNeighborOffsets =
{
    TemporaryNeighborOffset{  1,  0,  0 },
    TemporaryNeighborOffset{ -1,  1,  0 },
    TemporaryNeighborOffset{  0,  1,  0 },
    TemporaryNeighborOffset{  1,  1,  0 },
    TemporaryNeighborOffset{ -1, -1,  1 },
    TemporaryNeighborOffset{  0, -1,  1 },
    TemporaryNeighborOffset{  1, -1,  1 },
    TemporaryNeighborOffset{ -1,  0,  1 },
    TemporaryNeighborOffset{  0,  0,  1 },
    TemporaryNeighborOffset{  1,  0,  1 },
    TemporaryNeighborOffset{ -1,  1,  1 },
    TemporaryNeighborOffset{  0,  1,  1 },
    TemporaryNeighborOffset{  1,  1,  1 }
};

} // namespace

void SoftBodySpatialHashGrid::GenerateCandidatePairs(
    std::vector<
        SoftBodySpatialHashPair,
        SolverTemporaryAllocator<SoftBodySpatialHashPair>>& outPairs) const
{
    outPairs.clear();

    // 通常vector版と同じ「Occupied Cell + 13 Neighbor」走査です。
    // Candidate vectorのAllocatorだけが違い、Pair集合とPair順序は維持します。
    for (std::size_t activeBucketIndex : m_ActiveBucketIndices)
    {
        const CellBucket& centerBucket = m_Buckets[activeBucketIndex];
        const CellCoord& centerCell = centerBucket.Coord;
        const ParticleIndexBuffer& centerParticles = centerBucket.ParticleIndices;

        for (std::size_t firstIndex = 0u; firstIndex < centerParticles.Count; ++firstIndex)
        {
            for (std::size_t secondIndex = firstIndex + 1u;
                 secondIndex < centerParticles.Count;
                 ++secondIndex)
            {
                const uint32_t particleA = centerParticles.Storage.data()[firstIndex];
                const uint32_t particleB = centerParticles.Storage.data()[secondIndex];
                if (particleA == particleB)
                {
                    continue;
                }

                SoftBodySpatialHashPair pair{};
                pair.ParticleA = std::min(particleA, particleB);
                pair.ParticleB = std::max(particleA, particleB);
                outPairs.push_back(pair);
            }
        }

        for (const TemporaryNeighborOffset& offset : TemporaryUniqueNeighborOffsets)
        {
            CellCoord neighborCell{};
            neighborCell.X = centerCell.X + offset.X;
            neighborCell.Y = centerCell.Y + offset.Y;
            neighborCell.Z = centerCell.Z + offset.Z;

            const CellBucket* neighborBucket = FindActiveBucket(neighborCell);
            if (neighborBucket == nullptr)
            {
                continue;
            }

            const ParticleIndexBuffer& neighborParticles = neighborBucket->ParticleIndices;
            for (std::size_t centerIndex = 0u; centerIndex < centerParticles.Count; ++centerIndex)
            {
                const uint32_t centerParticle = centerParticles.Storage.data()[centerIndex];
                for (std::size_t neighborIndex = 0u;
                     neighborIndex < neighborParticles.Count;
                     ++neighborIndex)
                {
                    const uint32_t neighborParticle =
                        neighborParticles.Storage.data()[neighborIndex];
                    if (centerParticle == neighborParticle)
                    {
                        continue;
                    }

                    SoftBodySpatialHashPair pair{};
                    pair.ParticleA = std::min(centerParticle, neighborParticle);
                    pair.ParticleB = std::max(centerParticle, neighborParticle);
                    outPairs.push_back(pair);
                }
            }
        }
    }
}

void SoftBodyTriangleSpatialHashGrid::GenerateParticleTriangleCandidates(
    const std::vector<SoftBodyParticle>& particles,
    std::vector<
        SoftBodyParticleTrianglePair,
        SolverTemporaryAllocator<SoftBodyParticleTrianglePair>>& outPairs) const
{
    outPairs.clear();

    uint64_t cellCandidateCount = 0u;
    uint64_t expandedAABBRejectCount = 0u;
    uint64_t topologyRejectCount = 0u;
    uint64_t planeTestCount = 0u;
    uint64_t planeRejectCount = 0u;
    uint64_t edgeHalfSpaceTestCount = 0u;
    uint64_t edgeHalfSpaceRejectCount = 0u;

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const uint32_t particleIndex32 = static_cast<uint32_t>(particleIndex);
        const math::Vec3& particlePosition = particles[particleIndex].Position;
        const CellCoord cell = ComputeCellCoord(particlePosition);
        const TriangleCellBucket* bucket = FindActiveBucket(cell);
        if (bucket == nullptr)
        {
            continue;
        }

        for (std::size_t bucketTriangleIndex = 0u;
             bucketTriangleIndex < bucket->TriangleIndices.Count;
             ++bucketTriangleIndex)
        {
            const uint32_t triangleIndex =
                bucket->TriangleIndices.Storage.data()[bucketTriangleIndex];
            if (m_DetailedProfilingEnabled)
            {
                ++cellCandidateCount;
            }

            if (triangleIndex >= m_BuildBounds.size())
            {
                if (m_DetailedProfilingEnabled)
                {
                    ++expandedAABBRejectCount;
                }
                continue;
            }

            const TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];
            if (bounds.Valid == false
                || particlePosition.x < bounds.Minimum.x
                || particlePosition.x > bounds.Maximum.x
                || particlePosition.y < bounds.Minimum.y
                || particlePosition.y > bounds.Maximum.y
                || particlePosition.z < bounds.Minimum.z
                || particlePosition.z > bounds.Maximum.z)
            {
                if (m_DetailedProfilingEnabled)
                {
                    ++expandedAABBRejectCount;
                }
                continue;
            }

            if (bounds.ParticleA == particleIndex32
                || bounds.ParticleB == particleIndex32
                || bounds.ParticleC == particleIndex32)
            {
                if (m_DetailedProfilingEnabled)
                {
                    ++topologyRejectCount;
                }
                continue;
            }

            if (bounds.PlaneValid)
            {
                if (m_DetailedProfilingEnabled)
                {
                    ++planeTestCount;
                }

                const float signedScaledDistance =
                    math::Vec3::Dot(bounds.PlaneNormal, particlePosition) - bounds.PlaneOffset;
                if (signedScaledDistance * signedScaledDistance
                    > bounds.PlaneDistanceThresholdSq)
                {
                    if (m_DetailedProfilingEnabled)
                    {
                        ++planeRejectCount;
                    }
                    continue;
                }
            }

            if (bounds.EdgeHalfSpaceValid)
            {
                if (m_DetailedProfilingEnabled)
                {
                    ++edgeHalfSpaceTestCount;
                }

                bool edgeHalfSpaceRejected = false;

                const float edgeABSignedScaled =
                    math::Vec3::Dot(bounds.EdgeABNormal, particlePosition) - bounds.EdgeABOffset;
                if (edgeABSignedScaled < 0.0f
                    && edgeABSignedScaled * edgeABSignedScaled > bounds.EdgeABDistanceThresholdSq)
                {
                    edgeHalfSpaceRejected = true;
                }

                if (edgeHalfSpaceRejected == false)
                {
                    const float edgeBCSignedScaled =
                        math::Vec3::Dot(bounds.EdgeBCNormal, particlePosition) - bounds.EdgeBCOffset;
                    if (edgeBCSignedScaled < 0.0f
                        && edgeBCSignedScaled * edgeBCSignedScaled > bounds.EdgeBCDistanceThresholdSq)
                    {
                        edgeHalfSpaceRejected = true;
                    }
                }

                if (edgeHalfSpaceRejected == false)
                {
                    const float edgeCASignedScaled =
                        math::Vec3::Dot(bounds.EdgeCANormal, particlePosition) - bounds.EdgeCAOffset;
                    if (edgeCASignedScaled < 0.0f
                        && edgeCASignedScaled * edgeCASignedScaled > bounds.EdgeCADistanceThresholdSq)
                    {
                        edgeHalfSpaceRejected = true;
                    }
                }

                if (edgeHalfSpaceRejected)
                {
                    if (m_DetailedProfilingEnabled)
                    {
                        ++edgeHalfSpaceRejectCount;
                    }
                    continue;
                }
            }

            SoftBodyParticleTrianglePair pair{};
            pair.ParticleIndex = particleIndex32;
            pair.TriangleIndex = triangleIndex;
            outPairs.push_back(pair);
        }
    }

    // 通常vector版と同じProfiler Counterを送ります。
    // Counter付きvectorへ切り替えたことでCandidate funnel自体が変化していないかも確認できます。
    CPUProfiler& profiler = CPUProfiler::Get();
    if (m_DetailedProfilingEnabled && profiler.IsEnabled())
    {
        const double candidateCount = static_cast<double>(cellCandidateCount);
        const double aabbRejectCount = static_cast<double>(expandedAABBRejectCount);
        const double aabbRejectRatio = cellCandidateCount > 0u
            ? aabbRejectCount / candidateCount
            : 0.0;

        const uint64_t aabbPassCount = cellCandidateCount - expandedAABBRejectCount;
        const double topologyRejectCountValue = static_cast<double>(topologyRejectCount);
        const double topologyRejectRatio = aabbPassCount > 0u
            ? topologyRejectCountValue / static_cast<double>(aabbPassCount)
            : 0.0;

        const double planeTestCountValue = static_cast<double>(planeTestCount);
        const double planeRejectCountValue = static_cast<double>(planeRejectCount);
        const double planeRejectRatio = planeTestCount > 0u
            ? planeRejectCountValue / planeTestCountValue
            : 0.0;

        const double edgeHalfSpaceTestCountValue = static_cast<double>(edgeHalfSpaceTestCount);
        const double edgeHalfSpaceRejectCountValue = static_cast<double>(edgeHalfSpaceRejectCount);
        const double edgeHalfSpaceRejectRatio = edgeHalfSpaceTestCount > 0u
            ? edgeHalfSpaceRejectCountValue / edgeHalfSpaceTestCountValue
            : 0.0;
        const double closestPointCandidateCount = static_cast<double>(outPairs.size());

        profiler.AddCounter("SoftBody.TriangleHash.CellCandidateCount", candidateCount);
        profiler.AddCounter("SoftBody.TriangleHash.ExpandedAABBRejectCount", aabbRejectCount);
        profiler.AddCounter("SoftBody.TriangleHash.ExpandedAABBRejectRatio", aabbRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.TopologyRejectCount", topologyRejectCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.TopologyRejectRatio", topologyRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.PlaneTestCount", planeTestCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.PlaneRejectCount", planeRejectCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.PlaneRejectRatio", planeRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.EdgeHalfSpaceTestCount", edgeHalfSpaceTestCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.EdgeHalfSpaceRejectCount", edgeHalfSpaceRejectCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.EdgeHalfSpaceRejectRatio", edgeHalfSpaceRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.ClosestPointCandidateCount", closestPointCandidateCount);
    }
}

} // namespace ph
} // namespace Raven
