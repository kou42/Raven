#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Raven/Core/CPUProfiler.h"

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
    m_BuildBounds.clear();
    m_BuildCellRanges.clear();
    m_BucketMask = 0u;
    m_CurrentGeneration = 0u;
    m_ActiveCellCount = 0u;

    m_BuildRegistrationCount = 0u;
    m_BuildProbeCount = 0u;
    m_BuildMaxProbeCount = 0u;
    m_BuildVectorGrowCount = 0u;
    m_BuildTableGrowCount = 0u;
    m_BuildMaxCellSpanX = 0u;
    m_BuildMaxCellSpanY = 0u;
    m_BuildMaxCellSpanZ = 0u;
}

void SoftBodyTriangleSpatialHashGrid::BuildTriangles(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangle>& triangles,
    float expansion)
{
    const float safeExpansion = std::max(0.0f, expansion);
    std::size_t validTriangleCount = 0u;

    // ========================================================================
    // 1. Build preparation
    // ========================================================================
    // Generation更新・Flat Hash初期容量確認・Scratch resizeをまとめて計測します。
    // Scratchはmember vectorなので、2回目以降のXPBD iterationではcapacityを再利用できます。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.Prepare");

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

        m_BuildBounds.resize(triangles.size());
        m_BuildCellRanges.resize(triangles.size());

        // CellRegistrationの統計はBuild単位でリセットします。
        // Hot loop内では整数加算だけを行い、Profiler APIはBuild終了後にまとめて呼びます。
        m_BuildRegistrationCount = 0u;
        m_BuildProbeCount = 0u;
        m_BuildMaxProbeCount = 0u;
        m_BuildVectorGrowCount = 0u;
        m_BuildTableGrowCount = 0u;
        m_BuildMaxCellSpanX = 0u;
        m_BuildMaxCellSpanY = 0u;
        m_BuildMaxCellSpanZ = 0u;
    }

    // ========================================================================
    // 2. Triangle AABB
    // ========================================================================
    // Triangle頂点Positionからexpansion込みAABBだけを計算します。
    // Cell座標変換やHash探索を混ぜないことで、純粋なGeometry側コストを確認できます。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.AABB");

        for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
        {
            TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];
            bounds.Valid = false;

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

            bounds.Minimum.x = std::min({ a.x, b.x, c.x }) - safeExpansion;
            bounds.Minimum.y = std::min({ a.y, b.y, c.y }) - safeExpansion;
            bounds.Minimum.z = std::min({ a.z, b.z, c.z }) - safeExpansion;

            bounds.Maximum.x = std::max({ a.x, b.x, c.x }) + safeExpansion;
            bounds.Maximum.y = std::max({ a.y, b.y, c.y }) + safeExpansion;
            bounds.Maximum.z = std::max({ a.z, b.z, c.z }) + safeExpansion;
            bounds.Valid = true;
            ++validTriangleCount;
        }
    }

    // ========================================================================
    // 3. Cell Range conversion
    // ========================================================================
    // AABBのmin/maxをUniform Gridの整数Cell座標へ変換します。
    // std::floorを含む座標変換コストをCell登録ループから分離して確認できます。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRange");

        for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
        {
            TriangleBuildCellRange& cellRange = m_BuildCellRanges[triangleIndex];
            cellRange.Valid = false;

            const TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];
            if (bounds.Valid == false)
            {
                continue;
            }

            cellRange.Minimum = ComputeCellCoord(bounds.Minimum);
            cellRange.Maximum = ComputeCellCoord(bounds.Maximum);
            cellRange.Valid = true;

            // Cell SpanはTriangleが何セルへ広がっているかを見る指標です。
            // ここでは最大値だけを整数演算で集計し、登録ループへ追加コストを持ち込みません。
            const uint64_t spanX = static_cast<uint64_t>(
                static_cast<int64_t>(cellRange.Maximum.X)
                - static_cast<int64_t>(cellRange.Minimum.X) + 1ll);
            const uint64_t spanY = static_cast<uint64_t>(
                static_cast<int64_t>(cellRange.Maximum.Y)
                - static_cast<int64_t>(cellRange.Minimum.Y) + 1ll);
            const uint64_t spanZ = static_cast<uint64_t>(
                static_cast<int64_t>(cellRange.Maximum.Z)
                - static_cast<int64_t>(cellRange.Minimum.Z) + 1ll);

            m_BuildMaxCellSpanX = std::max(m_BuildMaxCellSpanX, spanX);
            m_BuildMaxCellSpanY = std::max(m_BuildMaxCellSpanY, spanY);
            m_BuildMaxCellSpanZ = std::max(m_BuildMaxCellSpanZ, spanZ);
        }
    }

    // ========================================================================
    // 4. Cell registration
    // ========================================================================
    // AABBが跨ぐ全Cellを列挙し、Flat HashからBucketを取得してTriangle Indexを追加します。
    // Hot loopではTimer/Mutex/文字列生成を行わず、整数Counterだけを更新します。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRegistration");

        for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
        {
            const TriangleBuildCellRange& cellRange = m_BuildCellRanges[triangleIndex];
            if (cellRange.Valid == false)
            {
                continue;
            }

            // TriangleはAABBが跨ぐ全Cellへ登録します。
            // Pass分離後もBroad Phaseの候補条件は旧実装から変更していません。
            for (int32_t z = cellRange.Minimum.Z; z <= cellRange.Maximum.Z; ++z)
            {
                for (int32_t y = cellRange.Minimum.Y; y <= cellRange.Maximum.Y; ++y)
                {
                    for (int32_t x = cellRange.Minimum.X; x <= cellRange.Maximum.X; ++x)
                    {
                        CellCoord cell{};
                        cell.X = x;
                        cell.Y = y;
                        cell.Z = z;

                        TriangleCellBucket& bucket = GetOrActivateBucket(cell);

                        // vector capacityが実際に増えた回数を数えます。
                        // これが多ければ、Flat HashではなくCell内TriangleIndicesのallocationが
                        // CellRegistration時間を支配している可能性があります。
                        const std::size_t previousCapacity = bucket.TriangleIndices.capacity();
                        bucket.TriangleIndices.push_back(static_cast<uint32_t>(triangleIndex));
                        if (bucket.TriangleIndices.capacity() != previousCapacity)
                        {
                            ++m_BuildVectorGrowCount;
                        }

                        ++m_BuildRegistrationCount;
                    }
                }
            }
        }
    }

    SubmitCellRegistrationCounters(validTriangleCount);
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
    ++m_BuildTableGrowCount;

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
    uint64_t probeCount = 0u;

    while (true)
    {
        ++probeCount;
        TriangleCellBucket& bucket = m_Buckets[bucketIndex];

        if (bucket.Generation != m_CurrentGeneration)
        {
            bucket.Coord = cell;
            bucket.TriangleIndices.clear();
            bucket.Generation = m_CurrentGeneration;
            ++m_ActiveCellCount;

            m_BuildProbeCount += probeCount;
            m_BuildMaxProbeCount = std::max(m_BuildMaxProbeCount, probeCount);
            return bucket;
        }

        if (bucket.Coord == cell)
        {
            m_BuildProbeCount += probeCount;
            m_BuildMaxProbeCount = std::max(m_BuildMaxProbeCount, probeCount);
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

void SoftBodyTriangleSpatialHashGrid::SubmitCellRegistrationCounters(
    std::size_t validTriangleCount) const
{
    CPUProfiler& profiler = CPUProfiler::Get();
    if (profiler.IsEnabled() == false)
    {
        return;
    }

    const double registrationCount = static_cast<double>(m_BuildRegistrationCount);
    const double activeCellCount = static_cast<double>(m_ActiveCellCount);
    const double triangleCount = static_cast<double>(validTriangleCount);

    const double averageCellsPerTriangle = validTriangleCount > 0u
        ? registrationCount / triangleCount
        : 0.0;
    const double averageTrianglesPerCell = m_ActiveCellCount > 0u
        ? registrationCount / activeCellCount
        : 0.0;
    const double averageProbeCount = m_BuildRegistrationCount > 0u
        ? static_cast<double>(m_BuildProbeCount) / registrationCount
        : 0.0;
    const double loadFactor = m_Buckets.empty() == false
        ? activeCellCount / static_cast<double>(m_Buckets.size())
        : 0.0;

    // 1 Buildにつき各Counterを1回だけ登録します。
    // StatisticsPanel側では12 iteration分をTotal / Average / Maxとして集計できます。
    profiler.AddCounter("SoftBody.TriangleHash.RegistrationCount", registrationCount);
    profiler.AddCounter("SoftBody.TriangleHash.ActiveCellCount", activeCellCount);
    profiler.AddCounter("SoftBody.TriangleHash.AverageCellsPerTriangle", averageCellsPerTriangle);
    profiler.AddCounter("SoftBody.TriangleHash.AverageTrianglesPerCell", averageTrianglesPerCell);
    profiler.AddCounter("SoftBody.TriangleHash.AverageProbeCount", averageProbeCount);
    profiler.AddCounter("SoftBody.TriangleHash.MaxProbeCount", static_cast<double>(m_BuildMaxProbeCount));
    profiler.AddCounter("SoftBody.TriangleHash.VectorGrowCount", static_cast<double>(m_BuildVectorGrowCount));
    profiler.AddCounter("SoftBody.TriangleHash.TableGrowCount", static_cast<double>(m_BuildTableGrowCount));
    profiler.AddCounter("SoftBody.TriangleHash.LoadFactor", loadFactor);
    profiler.AddCounter("SoftBody.TriangleHash.MaxCellSpanX", static_cast<double>(m_BuildMaxCellSpanX));
    profiler.AddCounter("SoftBody.TriangleHash.MaxCellSpanY", static_cast<double>(m_BuildMaxCellSpanY));
    profiler.AddCounter("SoftBody.TriangleHash.MaxCellSpanZ", static_cast<double>(m_BuildMaxCellSpanZ));
}

} // namespace ph
} // namespace Raven
