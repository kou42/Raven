#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumCellSize = 1.0e-4f;

// Inactive Bucketが増え続けないための最低Compaction閾値です。
// 通常のCloth solverでは同じ周辺Cellを反復して使うためCompactionはほぼ発生せず、
// 大きく移動し続けた場合だけ古いCell nodeを回収します。
constexpr std::size_t MinimumCompactionCellCount = 2048u;
constexpr std::size_t InactiveCellRetentionMultiplier = 4u;
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
    // Clear()は明示的な完全リセットです。
    // 通常のBuildTriangles()ではMapを破棄せずGenerationで論理クリアし、
    // Bucket内vectorのcapacityを次iterationへ再利用します。
    m_TriangleCells.clear();
    m_CurrentGeneration = 0u;
    m_ActiveCellCount = 0u;
}

void SoftBodyTriangleSpatialHashGrid::BuildTriangles(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangle>& triangles,
    float expansion)
{
    // ========================================================================
    // Generation-based logical clear
    // ========================================================================
    // 旧実装では毎iterationで unordered_map::clear() していたため、
    //   Cell node破棄
    //   -> 各vector破棄
    //   -> 次iterationでnode/vector再生成
    // が繰り返されていました。
    //
    // ClothのXPBD iteration間ではTriangle位置の変化は比較的小さく、使用Cellもほぼ同じです。
    // Map/Bucketを保持してGenerationだけ進めることで、前iterationのvector capacityを再利用します。
    ++m_CurrentGeneration;
    if (m_CurrentGeneration == 0u)
    {
        // uint32_tがwrapした場合だけ完全初期化します。
        // 実運用で到達する頻度ではありませんが、古いGenerationと一致する可能性を排除します。
        m_TriangleCells.clear();
        m_CurrentGeneration = 1u;
    }

    m_ActiveCellCount = 0u;

    // 初回Build時にある程度bucketを確保してrehash回数を抑えます。
    // Triangleは複数Cellへ登録されますが、Cellは隣接Triangle間で共有されるため、
    // Triangle数の2倍を初期目安にして過剰reserveを避けます。
    if (m_TriangleCells.empty())
    {
        const std::size_t reserveCount = std::max<std::size_t>(
            triangles.size() * 2u,
            64u);
        m_TriangleCells.reserve(reserveCount);
    }

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

        // Triangleは面積を持つため1セル登録では取りこぼします。
        // AABBが跨ぐ全セルへ登録し、Particle側は自分の1セルだけ問い合わせればよい形にします。
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

    CompactInactiveBucketsIfNeeded();
}

void SoftBodyTriangleSpatialHashGrid::GenerateParticleTriangleCandidates(
    const std::vector<SoftBodyParticle>& particles,
    std::vector<SoftBodyParticleTrianglePair>& outPairs) const
{
    outPairs.clear();

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const CellCoord cell = ComputeCellCoord(particles[particleIndex].Position);
        const auto cellIt = m_TriangleCells.find(cell);
        if (cellIt == m_TriangleCells.end())
        {
            continue;
        }

        // Generation方式では古いBucketをMapへ残してcapacityを再利用します。
        // 現Buildで触られていないBucketは候補として扱ってはいけません。
        const TriangleCellBucket& bucket = cellIt->second;
        if (bucket.Generation != m_CurrentGeneration)
        {
            continue;
        }

        for (uint32_t triangleIndex : bucket.TriangleIndices)
        {
            SoftBodyParticleTrianglePair pair{};
            pair.ParticleIndex = static_cast<uint32_t>(particleIndex);
            pair.TriangleIndex = triangleIndex;
            outPairs.push_back(pair);
        }
    }
}

std::size_t SoftBodyTriangleSpatialHashGrid::CellCoordHasher::operator()(const CellCoord& coord) const
{
    const std::size_t hashX = std::hash<int32_t>{}(coord.X);
    const std::size_t hashY = std::hash<int32_t>{}(coord.Y);
    const std::size_t hashZ = std::hash<int32_t>{}(coord.Z);

    std::size_t seed = hashX;
    seed ^= hashY + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    seed ^= hashZ + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    return seed;
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

SoftBodyTriangleSpatialHashGrid::TriangleCellBucket&
SoftBodyTriangleSpatialHashGrid::GetOrActivateBucket(const CellCoord& cell)
{
    auto [cellIt, inserted] = m_TriangleCells.try_emplace(cell);
    static_cast<void>(inserted);

    TriangleCellBucket& bucket = cellIt->second;
    if (bucket.Generation != m_CurrentGeneration)
    {
        // std::vector::clear() はsizeだけを0にしcapacityを保持します。
        // ここが今回の最適化の中心で、同じCellを使う次iterationでは再allocationを大幅に削減できます。
        bucket.TriangleIndices.clear();
        bucket.Generation = m_CurrentGeneration;
        ++m_ActiveCellCount;
    }

    return bucket;
}

void SoftBodyTriangleSpatialHashGrid::CompactInactiveBucketsIfNeeded()
{
    if (m_TriangleCells.size() <= MinimumCompactionCellCount)
    {
        return;
    }

    const std::size_t retainedCellLimit = std::max(
        MinimumCompactionCellCount,
        m_ActiveCellCount * InactiveCellRetentionMultiplier);

    if (m_TriangleCells.size() <= retainedCellLimit)
    {
        return;
    }

    // 大きく移動し続けたSoftBodyでは、再利用されない過去Cellを永久保持するとMapが肥大化します。
    // 通常iterationではこの条件へ入らないため、毎回全Mapを走査するコストは発生しません。
    for (auto cellIt = m_TriangleCells.begin(); cellIt != m_TriangleCells.end();)
    {
        if (cellIt->second.Generation != m_CurrentGeneration)
        {
            cellIt = m_TriangleCells.erase(cellIt);
        }
        else
        {
            ++cellIt;
        }
    }
}

} // namespace ph
} // namespace Raven
