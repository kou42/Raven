#include "Raven/Physics/Particle/ParticleSpatialHashGrid.h"

#include <algorithm>
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
// 広い検索や過去の巨大なBucket Tableでは、一時表を構築せず従来の検索を使います。
constexpr float MaximumPreparedCellRange = 2.0f;
constexpr std::size_t MaximumPreparedBucketSlots = 256u * 1024u;
constexpr std::size_t MaximumPreparedCellProbes = 1024u * 1024u;

std::size_t NextPowerOfTwo(std::size_t value)
{
    std::size_t result = 1u;
    while (result < value)
    {
        result <<= 1u;
    }
    return result;
}
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
    m_HasPreparedNeighbors = false;
    m_Buckets.clear();
    m_ActiveBucketIndices.clear();
    m_BucketMask = 0u;
    m_CurrentGeneration = 0u;
    m_ParticleCount = 0u;
}

void ParticleSpatialHashGrid::BeginBuild(std::size_t particleCount)
{
    m_HasPreparedNeighbors = false;
    EnsureBucketCapacity(particleCount);
    BeginBuildGeneration();
    m_ParticleCount = particleCount;
}

void ParticleSpatialHashGrid::AddParticle(
    uint32_t particleIndex,
    const math::Vec3& position)
{
    m_HasPreparedNeighbors = false;
    const CellCoord cell = ComputeCellCoord(position);
    CellBucket& bucket = GetOrActivateBucket(cell);
    bucket.ParticleIndices.Append(particleIndex);
}

void ParticleSpatialHashGrid::PrepareNeighborBuckets(float searchRadius)
{
    m_HasPreparedNeighbors = false;
    const float cellRange = std::ceil(std::max(0.0f, searchRadius) * m_InverseCellSize);
    if (m_ActiveBucketIndices.empty() || m_Buckets.size() > MaximumPreparedBucketSlots ||
        (cellRange <= MaximumPreparedCellRange) == false)
    {
        return;
    }
    const auto range = static_cast<int32_t>(cellRange);
    const auto diameter = static_cast<std::size_t>(range * 2 + 1);
    const std::size_t probesPerBucket = diameter * diameter * diameter;
    if (m_ActiveBucketIndices.size() > MaximumPreparedCellProbes / probesPerBucket)
    {
        return;
    }

    m_PreparedNeighborRanges.resize(m_Buckets.size());
    m_PreparedNeighborBuckets.clear();
    for (std::size_t centerIndex : m_ActiveBucketIndices)
    {
        auto& row = m_PreparedNeighborRanges[centerIndex];
        row.Begin = static_cast<uint32_t>(m_PreparedNeighborBuckets.size());
        ForEachNeighborBucket(m_Buckets[centerIndex].Coord, range, [&](const CellBucket& bucket)
        {
            m_PreparedNeighborBuckets.push_back(
                static_cast<uint32_t>(&bucket - m_Buckets.data()));
        });
        row.End = static_cast<uint32_t>(m_PreparedNeighborBuckets.size());
    }
    m_PreparedSearchRadius = searchRadius;
    m_HasPreparedNeighbors = true;
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

} // namespace ph
} // namespace Raven
