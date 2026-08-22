#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumCellSize = 1.0e-4f;

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
    m_Cells.clear();
    m_ParticleCount = 0u;
}

void SoftBodySpatialHashGrid::Build(const std::vector<SoftBodyParticle>& particles)
{
    m_Cells.clear();
    m_ParticleCount = particles.size();

    // ClothではOccupied Cell数はParticle数以下です。
    // あらかじめParticle数程度をreserveしておくことでBuild時のrehashを抑えます。
    // clear()後もunordered_mapのbucket_count自体は保持されるため、2回目以降のXPBD iterationでは
    // ほぼ再確保なしで利用できます。
    if (m_Cells.bucket_count() < particles.size())
    {
        m_Cells.reserve(particles.size());
    }

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const CellCoord cell = ComputeCellCoord(particles[particleIndex].Position);
        m_Cells[cell].push_back(static_cast<uint32_t>(particleIndex));
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
    for (const auto& cellEntry : m_Cells)
    {
        const CellCoord& centerCell = cellEntry.first;
        const std::vector<uint32_t>& centerParticles = cellEntry.second;

        // --------------------------------------------------------------------
        // Same Cell Pairs
        // --------------------------------------------------------------------
        // 同じCell内ではi<jの組み合わせだけを生成し、重複を完全に防ぎます。
        for (std::size_t firstIndex = 0u; firstIndex < centerParticles.size(); ++firstIndex)
        {
            for (std::size_t secondIndex = firstIndex + 1u;
                 secondIndex < centerParticles.size();
                 ++secondIndex)
            {
                AppendNormalizedPair(
                    centerParticles[firstIndex],
                    centerParticles[secondIndex],
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

            const auto neighborIt = m_Cells.find(neighborCell);
            if (neighborIt == m_Cells.end())
            {
                continue;
            }

            const std::vector<uint32_t>& neighborParticles = neighborIt->second;

            // 隣接する2 CellのParticleは全Cross PairがBroad Phase候補です。
            // Particle順はCell順とは無関係なのでAppendNormalizedPair()でA<Bへ揃えます。
            for (uint32_t centerParticle : centerParticles)
            {
                for (uint32_t neighborParticle : neighborParticles)
                {
                    AppendNormalizedPair(
                        centerParticle,
                        neighborParticle,
                        outPairs);
                }
            }
        }
    }
}

std::size_t SoftBodySpatialHashGrid::CellCoordHasher::operator()(const CellCoord& coord) const
{
    const std::size_t hashX = std::hash<int32_t>{}(coord.X);
    const std::size_t hashY = std::hash<int32_t>{}(coord.Y);
    const std::size_t hashZ = std::hash<int32_t>{}(coord.Z);

    std::size_t seed = hashX;
    seed ^= hashY + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    seed ^= hashZ + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    return seed;
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
