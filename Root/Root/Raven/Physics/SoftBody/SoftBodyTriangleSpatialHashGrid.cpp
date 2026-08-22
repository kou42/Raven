#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumCellSize = 1.0e-4f;
constexpr std::size_t MinimumBucketCount = 256u;
constexpr std::size_t InitialBucketMultiplier = 16u;
constexpr std::size_t MaximumLoadNumerator = 7u;
constexpr std::size_t MaximumLoadDenominator = 10u;

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

SoftBodyTriangleSpatialHashGrid::SoftBodyTriangleSpatialHashGrid(float cellSize)
{
    SetCellSize(cellSize);
}

void SoftBodyTriangleSpatialHashGrid::SetCellSize(float cellSize)
{
    m_CellSize = std::max(cellSize, MinimumCellSize);
    m_InverseCellSize = 1.0f / m_CellSize;
    Clear();
}

void SoftBodyTriangleSpatialHashGrid::Clear()
{
    // 明示的ClearではTable全体を破棄します。
    // 通常のBuildTriangles()ではGenerationだけを更新し、Bucketと各vector capacityを再利用します。
    m_Buckets.clear();
    m_BucketMask = 0u;
    m_CurrentGeneration = 0u;
    m_ActiveCellCount = 0u;
}

void SoftBodyTriangleSpatialHashGrid::BuildTriangles(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangle>& triangles,
    float expansion)
{
    // ========================================================================
    // Flat Hash frame generation
    // ========================================================================
    // unordered_map版と同様に毎iterationで物理Clearは行いません。
    // Generationが一致するBucketだけを現在BuildのActive Cellとして扱います。
    ++m_CurrentGeneration;
    if (m_CurrentGeneration == 0u)
    {
        // Generation wrap時は古い値との一致を避けるため全BucketをInactiveへ戻します。
        for (TriangleCellBucket& bucket : m_Buckets)
        {
            bucket.Generation = 0u;
        }
        m_CurrentGeneration = 1u;
    }

    m_ActiveCellCount = 0u;
    EnsureInitialBucketCapacity(triangles.size());

    const float safeExpansion = std::max(0.0f, expansion);

    for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
    {
        const SoftBodyTriangle& triangle = triangles[triangleIndex];
        if (triangle.ParticleA >= particles.size()
            || triangle.ParticleB >= particles.size()
            || triangle.ParticleC >= particles.size())
        {
            continue;
        }

        const math::Vec3& a = particles[triangle.ParticleA].Position;
        const math::Vec3& b = particles[triangle.ParticleB].Position;
        const math::Vec3& c = particles[triangle.ParticleC].Position;

        math::Vec3 minimum{};
        minimum.x = std::min({ a.x, b.x, c.x }) - safeExpansion;
        minimum.y = std::min({ a.y, b.y, c.y }) - safeExpansion;
        minimum.z = std::min({ a.z, b.z, c.z }) - safeExpansion;

        math::Vec3 maximum{};
        maximum.x = std::max({ a.x, b.x, c.x }) + safeExpansion;
        maximum.y = std::max({ a.y, b.y, c.y }) + safeExpansion;
        maximum.z = std::max({ a.z, b.z, c.z }) + safeExpansion;

        const CellCoord minimumCell = ComputeCellCoord(minimum);
        const CellCoord maximumCell = ComputeCellCoord(maximum);

        // TriangleはAABBが跨ぐ全Cellへ登録します。
        // Open Addressing化してもBroad Phaseの候補条件そのものは旧実装から変更しません。
        for (int32_t z = minimumCell.Z; z <= maximumCell.Z; ++z)
        {
            for (int32_t y = minimumCell.Y; y <= maximumCell.Y; ++y)
            {
                for (int32_t x = minimumCell.X; x <= maximumCell.X; ++x)
                {
                    CellCoord cell{};
                    cell.X = x;
                    cell.Y = y;
                    cell.Z = z;

                    TriangleCellBucket& bucket = GetOrActivateBucket(cell);
                    bucket.TriangleIndices.push_back(static_cast<uint32_t>(triangleIndex));
                }
            }
        }
    }
}

void SoftBodyTriangleSpatialHashGrid::GenerateParticleTriangleCandidates(
    const std::vector<SoftBodyParticle>& particles,
    std::vector<SoftBodyParticleTrianglePair>& outPairs) const
{
    outPairs.clear();

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const CellCoord cell = ComputeCellCoord(particles[particleIndex].Position);
        const TriangleCellBucket* bucket = FindActiveBucket(cell);
        if (bucket == nullptr)
        {
            continue;
        }

        for (uint32_t triangleIndex : bucket->TriangleIndices)
        {
            SoftBodyParticleTrianglePair pair{};
            pair.ParticleIndex = static_cast<uint32_t>(particleIndex);
            pair.TriangleIndex = triangleIndex;
            outPairs.push_back(pair);
        }
    }
}

SoftBodyTriangleSpatialHashGrid::CellCoord SoftBodyTriangleSpatialHashGrid::ComputeCellCoord(
    const math::Vec3& position) const
{
    CellCoord coord{};
    coord.X = static_cast<int32_t>(std::floor(position.x * m_InverseCellSize));
    coord.Y = static_cast<int32_t>(std::floor(position.y * m_InverseCellSize));
    coord.Z = static_cast<int32_t>(std::floor(position.z * m_InverseCellSize));
    return coord;
}

std::size_t SoftBodyTriangleSpatialHashGrid::HashCell(const CellCoord& cell) const
{
    // 3軸へ異なる大きな素数を掛けてXORします。
    // Cell座標は負値も取り得るためuint32_tへbit-patternとして変換してから混合します。
    const uint64_t x = static_cast<uint32_t>(cell.X);
    const uint64_t y = static_cast<uint32_t>(cell.Y);
    const uint64_t z = static_cast<uint32_t>(cell.Z);

    uint64_t hash = x * 73856093ull;
    hash ^= y * 19349663ull;
    hash ^= z * 83492791ull;

    // 上位bitも下位bitへ混ぜ、2の累乗Maskを使った場合の偏りを抑えます。
    hash ^= hash >> 33u;
    hash *= 0xff51afd7ed558ccdull;
    hash ^= hash >> 33u;

    return static_cast<std::size_t>(hash);
}

void SoftBodyTriangleSpatialHashGrid::EnsureInitialBucketCapacity(std::size_t triangleCount)
{
    if (m_Buckets.empty() == false)
    {
        return;
    }

    // Cloth TriangleはAABB expansionによって複数Cellへ登録されます。
    // Triangle数の16倍を初期目安にし、Load Factorを低く保ってLinear Probe回数を抑えます。
    const std::size_t requestedBucketCount = std::max(
        MinimumBucketCount,
        triangleCount * InitialBucketMultiplier);

    const std::size_t bucketCount = NextPowerOfTwo(requestedBucketCount);
    m_Buckets.resize(bucketCount);
    m_BucketMask = bucketCount - 1u;
}

void SoftBodyTriangleSpatialHashGrid::GrowBuckets()
{
    const std::size_t oldBucketCount = m_Buckets.size();
    const std::size_t newBucketCount = std::max(
        MinimumBucketCount,
        oldBucketCount == 0u ? MinimumBucketCount : oldBucketCount * 2u);

    std::vector<TriangleCellBucket> oldBuckets = std::move(m_Buckets);
    m_Buckets.clear();
    m_Buckets.resize(newBucketCount);
    m_BucketMask = newBucketCount - 1u;

    const std::size_t previousActiveCellCount = m_ActiveCellCount;
    m_ActiveCellCount = 0u;

    // 現GenerationのBucketだけを新Tableへ移します。
    // TriangleIndicesはmoveするため、緊急Grow時でも既存vector allocationを可能な限り保持します。
    for (TriangleCellBucket& oldBucket : oldBuckets)
    {
        if (oldBucket.Generation != m_CurrentGeneration)
        {
            continue;
        }

        std::size_t bucketIndex = HashCell(oldBucket.Coord) & m_BucketMask;
        while (true)
        {
            TriangleCellBucket& newBucket = m_Buckets[bucketIndex];
            if (newBucket.Generation != m_CurrentGeneration)
            {
                newBucket.Coord = oldBucket.Coord;
                newBucket.TriangleIndices = std::move(oldBucket.TriangleIndices);
                newBucket.Generation = m_CurrentGeneration;
                ++m_ActiveCellCount;
                break;
            }

            bucketIndex = (bucketIndex + 1u) & m_BucketMask;
        }
    }

    // Grow前後でActive Cell数が変わる場合は内部不整合です。
    // Release buildでも処理継続できるよう値を復元するような分岐は行わず、
    // 正常系では必ず一致する単純な構造にしています。
    static_cast<void>(previousActiveCellCount);
}

SoftBodyTriangleSpatialHashGrid::TriangleCellBucket&
SoftBodyTriangleSpatialHashGrid::GetOrActivateBucket(const CellCoord& cell)
{
    if (m_Buckets.empty())
    {
        EnsureInitialBucketCapacity(0u);
    }

    // Linear Probeが長くなる前にTableを拡張します。
    // active + 1 が70%を越える場合だけGrowするため、通常iterationでは発生しません。
    if ((m_ActiveCellCount + 1u) * MaximumLoadDenominator
        > m_Buckets.size() * MaximumLoadNumerator)
    {
        GrowBuckets();
    }

    std::size_t bucketIndex = HashCell(cell) & m_BucketMask;

    while (true)
    {
        TriangleCellBucket& bucket = m_Buckets[bucketIndex];

        if (bucket.Generation != m_CurrentGeneration)
        {
            // Inactive Bucketは現在Buildでは空なので、その場で再利用できます。
            // clear()はcapacityを保持するため、同じSlotが再利用された場合のallocationを削減できます。
            bucket.Coord = cell;
            bucket.TriangleIndices.clear();
            bucket.Generation = m_CurrentGeneration;
            ++m_ActiveCellCount;
            return bucket;
        }

        if (bucket.Coord == cell)
        {
            return bucket;
        }

        bucketIndex = (bucketIndex + 1u) & m_BucketMask;
    }
}

const SoftBodyTriangleSpatialHashGrid::TriangleCellBucket*
SoftBodyTriangleSpatialHashGrid::FindActiveBucket(const CellCoord& cell) const
{
    if (m_Buckets.empty())
    {
        return nullptr;
    }

    std::size_t bucketIndex = HashCell(cell) & m_BucketMask;

    while (true)
    {
        const TriangleCellBucket& bucket = m_Buckets[bucketIndex];

        // 現Generationで未使用のSlotに到達した時点で、このProbe列には対象Cellが存在しません。
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

} // namespace ph
} // namespace Raven
