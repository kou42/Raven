#include "Raven/Physics/Fluid/FluidSpatialHashGrid.h"

#include "Raven/Core/CPUProfiler.h"

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
    RAVEN_PROFILE_SCOPE("Physics.Fluid.SPH.SpatialHashBuild");

    m_Core.BeginBuild(particles.size());

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        m_Core.AddParticle(
            static_cast<uint32_t>(particleIndex),
            particles[particleIndex].Position);
    }

    // CellSize比較では「Build時間」だけでなく、同じParticle群が何Cellへ分散したかも重要です。
    // Neighbor候補Counterと合わせて見ることで、小さ過ぎるCellによる走査Cell数増加と、
    // 大き過ぎるCellによる候補Particle増加を区別できるようにします。
    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.AddCounter("Physics.Fluid.SPH.SpatialHashCellSize",
        static_cast<double>(m_Core.GetCellSize()));
    profiler.AddCounter("Physics.Fluid.SPH.SpatialHashOccupiedCellCount",
        static_cast<double>(m_Core.GetOccupiedCellCount()));
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
