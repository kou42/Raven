#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"

namespace Raven
{
namespace ph
{

SoftBodySpatialHashGrid::SoftBodySpatialHashGrid(float cellSize)
    : m_Core(cellSize)
{
}

void SoftBodySpatialHashGrid::SetCellSize(float cellSize)
{
    m_Core.SetCellSize(cellSize);
}

float SoftBodySpatialHashGrid::GetCellSize() const
{
    return m_Core.GetCellSize();
}

void SoftBodySpatialHashGrid::Clear()
{
    m_Core.Clear();
}

void SoftBodySpatialHashGrid::Build(const std::vector<SoftBodyParticle>& particles)
{
    m_Core.BeginBuild(particles.size());

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        m_Core.AddParticle(
            static_cast<uint32_t>(particleIndex),
            particles[particleIndex].Position);
    }
}

std::size_t SoftBodySpatialHashGrid::GetOccupiedCellCount() const
{
    return m_Core.GetOccupiedCellCount();
}

std::size_t SoftBodySpatialHashGrid::GetParticleCount() const
{
    return m_Core.GetParticleCount();
}

} // namespace ph
} // namespace Raven
