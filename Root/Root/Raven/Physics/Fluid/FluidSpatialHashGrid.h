#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Particle/ParticleSpatialHashGrid.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Fluid Spatial Hash Adapter
// ============================================================================
// FluidParticle固有のBuild処理だけを担当し、Hash実装そのものはParticleSpatialHashGridへ委譲します。
// 継承ではなくcompositionにすることで、Fluid側からCore内部表現へ依存しない境界を保ちます。
class FluidSpatialHashGrid
{
public:
    explicit FluidSpatialHashGrid(float cellSize = 0.05f);

    void SetCellSize(float cellSize);
    float GetCellSize() const;

    void Clear();
    void Build(const std::vector<FluidParticle>& particles);

    template <typename Visitor>
    void ForEachNeighborParticle(
        const math::Vec3& position,
        float searchRadius,
        Visitor&& visitor) const
    {
        m_Core.ForEachNeighborParticle(
            position,
            searchRadius,
            std::forward<Visitor>(visitor));
    }

    std::size_t GetOccupiedCellCount() const;
    std::size_t GetParticleCount() const;

private:
    ParticleSpatialHashGrid m_Core;
};

} // namespace ph
} // namespace Raven
