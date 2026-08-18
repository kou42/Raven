#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumCellSize = 1.0e-4f;
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
    m_ParticlePositions.clear();
}

void SoftBodySpatialHashGrid::Build(const std::vector<SoftBodyParticle>& particles)
{
    m_Cells.clear();
    m_ParticlePositions.clear();
    m_ParticlePositions.reserve(particles.size());

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const math::Vec3& position = particles[particleIndex].Position;
        m_ParticlePositions.push_back(position);

        const CellCoord cell = ComputeCellCoord(position);
        m_Cells[cell].push_back(static_cast<uint32_t>(particleIndex));
    }
}

void SoftBodySpatialHashGrid::GenerateCandidatePairs(
    std::vector<SoftBodySpatialHashPair>& outPairs) const
{
    outPairs.clear();

    // Particleは必ず1つの基準Cellにだけ登録されます。
    // 各Particleから周囲27Cellを見る一方で neighborIndex <= particleIndex を除外するため、
    // (A,B) と (B,A) の二重登録も、同一Pairの重複登録も発生しません。
    for (std::size_t particleIndex = 0u; particleIndex < m_ParticlePositions.size(); ++particleIndex)
    {
        const CellCoord centerCell = ComputeCellCoord(m_ParticlePositions[particleIndex]);

        for (int32_t zOffset = -1; zOffset <= 1; ++zOffset)
        {
            for (int32_t yOffset = -1; yOffset <= 1; ++yOffset)
            {
                for (int32_t xOffset = -1; xOffset <= 1; ++xOffset)
                {
                    CellCoord neighborCell{};
                    neighborCell.X = centerCell.X + xOffset;
                    neighborCell.Y = centerCell.Y + yOffset;
                    neighborCell.Z = centerCell.Z + zOffset;

                    const auto cellIt = m_Cells.find(neighborCell);
                    if (cellIt == m_Cells.end())
                    {
                        continue;
                    }

                    for (uint32_t neighborIndex : cellIt->second)
                    {
                        if (neighborIndex <= particleIndex)
                        {
                            continue;
                        }

                        SoftBodySpatialHashPair pair{};
                        pair.ParticleA = static_cast<uint32_t>(particleIndex);
                        pair.ParticleB = neighborIndex;
                        outPairs.push_back(pair);
                    }
                }
            }
        }
    }
}

std::size_t SoftBodySpatialHashGrid::CellCoordHasher::operator()(const CellCoord& coord) const
{
    // hash値そのものに一意性は要求しません。
    // unordered_mapは最終的にCellCoord::operator==で同一Cellか確認するため、負座標を含めても
    // hash collisionによって別セルが同一セル扱いされることはありません。
    const std::size_t hashX = std::hash<int32_t>{}(coord.X);
    const std::size_t hashY = std::hash<int32_t>{}(coord.Y);
    const std::size_t hashZ = std::hash<int32_t>{}(coord.Z);

    // boost::hash_combineと同種のmixです。
    // 特定の素数積へ座標を直接畳み込む方式より、CellCoord equalityを残したまま扱える点を優先します。
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

} // namespace ph
} // namespace Raven
