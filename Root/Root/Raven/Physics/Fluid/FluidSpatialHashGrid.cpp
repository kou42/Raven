#include "Raven/Physics/Fluid/FluidSpatialHashGrid.h"

namespace Raven
{
namespace ph
{

FluidSpatialHashGrid::FluidSpatialHashGrid(float cellSize)
    : m_Core(cellSize)
{
}

void FluidSpatialHashGrid::SetCellSize(float cellSize)
{
    m_Core.SetCellSize(cellSize);
}

float FluidSpatialHashGrid::GetCellSize() const
{
    return m_Core.GetCellSize();
}

void FluidSpatialHashGrid::Clear()
{
    m_Core.Clear();
}

void FluidSpatialHashGrid::Build(const std::vector<FluidParticle>& particles)
{
    m_Core.BeginBuild(particles.size());

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        m_Core.AddParticle(
            static_cast<uint32_t>(particleIndex),
            particles[particleIndex].Position);
    }
}

std::size_t FluidSpatialHashGrid::GetOccupiedCellCount() const
{
    return m_Core.GetOccupiedCellCount();
}

std::size_t FluidSpatialHashGrid::GetParticleCount() const
{
    return m_Core.GetParticleCount();
}

} // namespace ph
} // namespace Raven
