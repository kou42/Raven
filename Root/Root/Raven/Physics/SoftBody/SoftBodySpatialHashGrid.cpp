#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumCellSize = 1.0e-4f;
constexpr std::size_t MinimumBucketCount = 256u;
constexpr std::size_t BucketCapacityMultiplier = 2u;

std::size_t NextPowerOfTwo(std::size_t value)
{
    std::size_t result = 1u;
    while (result < value)
    {
        result <<= 1u;
    }
    return result;
}

struct NeighborOffset
{
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Z = 0;
};

// ============================================================================
// Unique Neighbor Offsets
// ============================================================================
// 3x3x3近傍には自身を除いて26 Cellあります。
// その全てを各Cellから見ると A->B と B->A を二重処理するため、辞書順で「正方向」の
// 13 Cellだけを保持します。
//
// z > 0
// または z == 0 かつ y > 0
// または z == 0 かつ y == 0 かつ x > 0
//
// という半空間だけを見ることで、隣接Cell Pairを厳密に1回だけ処理できます。
constexpr std::array<NeighborOffset, 13u> UniqueNeighborOffsets =
{
    NeighborOffset{  1,  0,  0 },

    NeighborOffset{ -1,  1,  0 },
    NeighborOffset{  0,  1,  0 },
    NeighborOffset{  1,  1,  0 },

    NeighborOffset{ -1, -1,  1 },
    NeighborOffset{  0, -1,  1 },
    NeighborOffset{  1, -1,  1 },
    NeighborOffset{ -1,  0,  1 },
    NeighborOffset{  0,  0,  1 },
    NeighborOffset{  1,  0,  1 },
    NeighborOffset{ -1,  1,  1 },
    NeighborOffset{  0,  1,  1 },
    NeighborOffset{  1,  1,  1 }
};
}

SoftBodySpatialHashGrid::SoftBodySpatialHashGrid(float cellSize)
{
    SetCellSize(cellSize);
}

void SoftBodySpatialHashGrid::SetCellSize(float cellSize)
{
    // 0以下や極端に小さいCellSizeでは除算が不安定になるため下限を持たせます。
    // Self Collision Diameterとの整合は呼び出し側の設定責務ですが、Broad Phase単体でも
    // 0除算や巨大なセル座標を発生させないよう最低限の防御を行います。
    m_CellSize = std::max(cellSize, MinimumCellSize);
    m_InverseCellSize = 1.0f / m_CellSize;

    // CellSizeが変わると既存CellCoordの意味が変わるため、登録内容は破棄します。
    Clear();
}

void SoftBodySpatialHashGrid::Clear()
{
    m_Buckets.clear();
    m_ActiveBucketIndices.clear();
    m_BucketMask = 0u;
    m_CurrentGeneration = 0u;
    m_ParticleCount = 0u;
}

void SoftBodySpatialHashGrid::Build(const std::vector<SoftBodyParticle>& particles)
{
    EnsureBucketCapacity(particles.size());
    BeginBuildGeneration();
    m_ParticleCount = particles.size();

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const CellCoord cell = ComputeCellCoord(particles[particleIndex].Position);
        CellBucket& bucket = GetOrActivateBucket(cell);
        bucket.ParticleIndices.Append(static_cast<uint32_t>(particleIndex));
    }
}

void SoftBodySpatialHashGrid::GenerateCandidatePairs(
    std::vector<SoftBodySpatialHashPair>& outPairs) const
{
    // vector capacityは呼び出し側でiteration間に再利用されるため、clear()だけにします。
    outPairs.clear();

    // ========================================================================
    // Cell-based Pair Generation
    // ========================================================================
    // 旧実装はParticle N個それぞれについて27回unordered_map::find()していました。
    // 例えば1000 Particleなら約27000回/iterationのHash検索です。
    //
    // 新実装ではOccupied Cell C個について13 Neighborだけを見るため、最大でも約13*C回です。
    // また同一Cell内PairはMap検索不要で直接生成します。
    for (std::size_t activeBucketIndex : m_ActiveBucketIndices)
    {
        const CellBucket& centerBucket = m_Buckets[activeBucketIndex];
        const CellCoord& centerCell = centerBucket.Coord;
        const ParticleIndexBuffer& centerParticles = centerBucket.ParticleIndices;

        // --------------------------------------------------------------------
        // Same Cell Pairs
        // --------------------------------------------------------------------
        // 同じCell内ではi<jの組み合わせだけを生成し、重複を完全に防ぎます。
        for (std::size_t firstIndex = 0u; firstIndex < centerParticles.Count; ++firstIndex)
        {
            for (std::size_t secondIndex = firstIndex + 1u;
                 secondIndex < centerParticles.Count;
                 ++secondIndex)
            {
                AppendNormalizedPair(
                    centerParticles.Storage.data()[firstIndex],
                    centerParticles.Storage.data()[secondIndex],
                    outPairs);
            }
        }

        // --------------------------------------------------------------------
        // Neighbor Cell Cross Pairs
        // --------------------------------------------------------------------
        // 26方向全てではなく13方向だけを見るため、Cell Pairそのものを1回だけ処理します。
        for (const NeighborOffset& offset : UniqueNeighborOffsets)
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

            // 隣接する2 CellのParticleは全Cross PairがBroad Phase候補です。
            // Particle順はCell順とは無関係なのでAppendNormalizedPair()でA<Bへ揃えます。
            for (std::size_t centerIndex = 0u; centerIndex < centerParticles.Count; ++centerIndex)
            {
                const uint32_t centerParticle = centerParticles.Storage.data()[centerIndex];
                for (std::size_t neighborIndex = 0u;
                     neighborIndex < neighborParticles.Count;
                     ++neighborIndex)
                {
                    const uint32_t neighborParticle =
                        neighborParticles.Storage.data()[neighborIndex];
                    AppendNormalizedPair(
                        centerParticle,
                        neighborParticle,
                        outPairs);
                }
            }
        }
    }
}

std::size_t SoftBodySpatialHashGrid::HashCell(const CellCoord& cell) const
{
    const uint32_t hash =
        static_cast<uint32_t>(cell.X) * 73856093u
        ^ static_cast<uint32_t>(cell.Y) * 19349663u
        ^ static_cast<uint32_t>(cell.Z) * 83492791u;
    return static_cast<std::size_t>(hash) & m_BucketMask;
}

void SoftBodySpatialHashGrid::EnsureBucketCapacity(std::size_t particleCount)
{
    const std::size_t requiredBucketCount = NextPowerOfTwo(std::max(
        MinimumBucketCount,
        particleCount * BucketCapacityMultiplier));
    if (m_Buckets.size() >= requiredBucketCount)
    {
        if (m_ActiveBucketIndices.capacity() < particleCount)
        {
            m_ActiveBucketIndices.reserve(particleCount);
        }
        return;
    }

    // 容量変更時だけ旧Tableを破棄します。通常のiteration/frameでは同じTableと各Bufferを再利用します。
    m_Buckets.clear();
    m_Buckets.resize(requiredBucketCount);
    m_ActiveBucketIndices.clear();
    m_ActiveBucketIndices.reserve(particleCount);
    m_BucketMask = requiredBucketCount - 1u;
    m_CurrentGeneration = 0u;
}

void SoftBodySpatialHashGrid::BeginBuildGeneration()
{
    ++m_CurrentGeneration;
    if (m_CurrentGeneration == 0u)
    {
        for (CellBucket& bucket : m_Buckets)
        {
            bucket.Generation = 0u;
        }
        m_CurrentGeneration = 1u;
    }

    m_ActiveBucketIndices.clear();
}

SoftBodySpatialHashGrid::CellBucket& SoftBodySpatialHashGrid::GetOrActivateBucket(
    const CellCoord& cell)
{
    std::size_t bucketIndex = HashCell(cell);
    while (true)
    {
        CellBucket& bucket = m_Buckets[bucketIndex];
        if (bucket.Generation != m_CurrentGeneration)
        {
            bucket.Coord = cell;
            bucket.ParticleIndices.Reset();
            bucket.Generation = m_CurrentGeneration;
            m_ActiveBucketIndices.push_back(bucketIndex);
            return bucket;
        }

        if (bucket.Coord == cell)
        {
            return bucket;
        }

        bucketIndex = (bucketIndex + 1u) & m_BucketMask;
    }
}

const SoftBodySpatialHashGrid::CellBucket* SoftBodySpatialHashGrid::FindActiveBucket(
    const CellCoord& cell) const
{
    if (m_Buckets.empty())
    {
        return nullptr;
    }

    std::size_t bucketIndex = HashCell(cell);
    while (true)
    {
        const CellBucket& bucket = m_Buckets[bucketIndex];
        if (bucket.Generation != m_CurrentGeneration)
        {
            return nullptr;
        }

        if (bucket.Coord == cell)
        {
            return &bucket;
        }

        bucketIndex = (bucketIndex + 1u) & m_BucketMask;
    }
}

SoftBodySpatialHashGrid::CellCoord SoftBodySpatialHashGrid::ComputeCellCoord(
    const math::Vec3& position) const
{
    CellCoord coord{};

    // static_cast<int32_t>(-0.2f) は0方向へ切り捨てられますが、Uniform Gridで必要なのは
    // 数直線上のセル境界に対するfloorです。そのため負座標でも必ずstd::floorを通します。
    coord.X = static_cast<int32_t>(std::floor(position.x * m_InverseCellSize));
    coord.Y = static_cast<int32_t>(std::floor(position.y * m_InverseCellSize));
    coord.Z = static_cast<int32_t>(std::floor(position.z * m_InverseCellSize));
    return coord;
}

void SoftBodySpatialHashGrid::AppendNormalizedPair(
    uint32_t particleA,
    uint32_t particleB,
    std::vector<SoftBodySpatialHashPair>& outPairs)
{
    if (particleA == particleB)
    {
        return;
    }

    SoftBodySpatialHashPair pair{};
    pair.ParticleA = std::min(particleA, particleB);
    pair.ParticleB = std::max(particleA, particleB);
    outPairs.push_back(pair);
}

} // namespace ph
} // namespace Raven
