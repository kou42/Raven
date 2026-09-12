#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"

namespace Raven
{
namespace ph
{

SoftBodySpatialHashGrid::SoftBodySpatialHashGrid(float cellSize)
    : ParticleSpatialHashGrid(cellSize)
{
}

void SoftBodySpatialHashGrid::Build(const std::vector<SoftBodyParticle>& particles)
{
    BeginBuild(particles.size());

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        AddParticle(
            static_cast<uint32_t>(particleIndex),
            particles[particleIndex].Position);
    }
}

} // namespace ph
} // namespace Raven
