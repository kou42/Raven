#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

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
    m_TriangleCells.clear();
}

void SoftBodyTriangleSpatialHashGrid::BuildTriangles(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangle>& triangles,
    float expansion)
{
    m_TriangleCells.clear();

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
                    m_TriangleCells[cell].push_back(static_cast<uint32_t>(triangleIndex));
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
        const auto cellIt = m_TriangleCells.find(cell);
        if (cellIt == m_TriangleCells.end())
        {
            continue;
        }

        for (uint32_t triangleIndex : cellIt->second)
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

} // namespace ph
} // namespace Raven
