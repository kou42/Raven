#include "Raven/Physics/Particle/ParticleSpatialHashGrid.h"

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

// 3x3x3近傍のうち辞書順で正方向となる13 Cellだけを走査し、
// Cell A -> B と Cell B -> A の二重処理を防ぎます。
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

ParticleSpatialHashGrid::ParticleSpatialHashGrid(float cellSize)
{
    SetCellSize(cellSize);
}

void ParticleSpatialHashGrid::SetCellSize(float cellSize)
{
    // 0以下や極端に小さいCellSizeでは除算が不安定になるため下限を持たせます。
    m_CellSize = std::max(cellSize, MinimumCellSize);
    m_InverseCellSize = 1.0f / m_CellSize;

    // CellSizeが変わると既存CellCoordの意味が変わるため、登録内容は破棄します。
    Clear();
}

void ParticleSpatialHashGrid::Clear()
{
    m_Buckets.clear();
    m_ActiveBucketIndices.clear();
    m_BucketMask = 0u;
    m_CurrentGeneration = 0u;
    m_ParticleCount = 0u;
}

void ParticleSpatialHashGrid::BeginBuild(std::size_t particleCount)
{
    EnsureBucketCapacity(particleCount);
    BeginBuildGeneration();
    m_ParticleCount = particleCount;
}

void ParticleSpatialHashGrid::AddParticle(
    uint32_t particleIndex,
    const math::Vec3& position)
{
    const CellCoord cell = ComputeCellCoord(position);
    CellBucket& bucket = GetOrActivateBucket(cell);
    bucket.ParticleIndices.Append(particleIndex);
}

void ParticleSpatialHashGrid::GenerateCandidatePairs(
    std::vector<ParticleSpatialHashPair>& outPairs) const
{
    // vector capacityは呼び出し側でiteration間に再利用できるようclear()だけにします。
    outPairs.clear();

    for (std::size_t activeBucketIndex : m_ActiveBucketIndices)
    {
        const CellBucket& centerBucket = m_Buckets[activeBucketIndex];
        const CellCoord& centerCell = centerBucket.Coord;
        const ParticleIndexBuffer& centerParticles = centerBucket.ParticleIndices;

        // 同一Cell内はi<jだけを生成します。
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

        // 全26方向ではなく13方向だけを見ることで、隣接Cell Pairを厳密に1回だけ処理します。
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
            for (std::size_t centerIndex = 0u; centerIndex < centerParticles.Count; ++centerIndex)
            {
                const uint32_t centerParticle = centerParticles.Storage.data()[centerIndex];
                for (std::size_t neighborIndex = 0u;
                     neighborIndex < neighborParticles.Count;
                     ++neighborIndex)
                {
                    const uint32_t neighborParticle =
                        neighborParticles.Storage.data()[neighborIndex];
                    AppendNormalizedPair(centerParticle, neighborParticle, outPairs);
                }
            }
        }
    }
}

std::size_t ParticleSpatialHashGrid::HashCell(const CellCoord& cell) const
{
    const uint32_t hash =
        static_cast<uint32_t>(cell.X) * 73856093u
        ^ static_cast<uint32_t>(cell.Y) * 19349663u
        ^ static_cast<uint32_t>(cell.Z) * 83492791u;
    return static_cast<std::size_t>(hash) & m_BucketMask;
}

void ParticleSpatialHashGrid::EnsureBucketCapacity(std::size_t particleCount)
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

    // 容量変更時だけ旧Tableを破棄し、通常iteration/frameではBucketを再利用します。
    m_Buckets.clear();
    m_Buckets.resize(requiredBucketCount);
    m_ActiveBucketIndices.clear();
    m_ActiveBucketIndices.reserve(particleCount);
    m_BucketMask = requiredBucketCount - 1u;
    m_CurrentGeneration = 0u;
}

void ParticleSpatialHashGrid::BeginBuildGeneration()
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

ParticleSpatialHashGrid::CellBucket& ParticleSpatialHashGrid::GetOrActivateBucket(
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

const ParticleSpatialHashGrid::CellBucket* ParticleSpatialHashGrid::FindActiveBucket(
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

ParticleSpatialHashGrid::CellCoord ParticleSpatialHashGrid::ComputeCellCoord(
    const math::Vec3& position) const
{
    CellCoord coord{};

    // intへのcastは負数を0方向へ切り捨てるため、Uniform Gridではfloorを明示します。
    coord.X = static_cast<int32_t>(std::floor(position.x * m_InverseCellSize));
    coord.Y = static_cast<int32_t>(std::floor(position.y * m_InverseCellSize));
    coord.Z = static_cast<int32_t>(std::floor(position.z * m_InverseCellSize));
    return coord;
}

void ParticleSpatialHashGrid::AppendNormalizedPair(
    uint32_t particleA,
    uint32_t particleB,
    std::vector<ParticleSpatialHashPair>& outPairs)
{
    if (particleA == particleB)
    {
        return;
    }

    ParticleSpatialHashPair pair{};
    pair.ParticleA = std::min(particleA, particleB);
    pair.ParticleB = std::max(particleA, particleB);
    outPairs.push_back(pair);
}

} // namespace ph
} // namespace Raven
