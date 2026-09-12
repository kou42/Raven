#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Physics/Particle/ParticleSpatialHashGrid.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"

namespace Raven
{
namespace ph
{

// SoftBody側の既存API互換名です。
// 実体はParticle系Simulationで共通利用するPair型へ統一します。
using SoftBodySpatialHashPair = ParticleSpatialHashPair;

// ============================================================================
// Soft Body Spatial Hash Adapter
// ============================================================================
// SoftBodyParticle固有のBuild処理だけを担当し、Hash実装そのものは
// ParticleSpatialHashGridへcompositionで委譲します。
//
// 継承を使わないことでSoftBody側からCore内部表現へ触れる経路を閉じ、Fluid / Molecular
// Dynamicsなど別Simulationと同じ「Particle型Adapter -> 共通Spatial Query」という境界に揃えます。
class SoftBodySpatialHashGrid
{
public:
    explicit SoftBodySpatialHashGrid(float cellSize = 0.05f);

    void SetCellSize(float cellSize);
    float GetCellSize() const;

    void Clear();

    // 現在のSoftBody Particle PositionからGridを再構築します。
    void Build(const std::vector<SoftBodyParticle>& particles);

    // PairContainerのAllocatorには依存しません。
    // 通常std::vectorとSolverTemporaryAllocator版の双方が同じCore走査を利用します。
    template <typename PairContainer>
    void GenerateCandidatePairs(PairContainer& outPairs) const
    {
        m_Core.GenerateCandidatePairs(outPairs);
    }

    std::size_t GetOccupiedCellCount() const;
    std::size_t GetParticleCount() const;

private:
    ParticleSpatialHashGrid m_Core;
};

} // namespace ph
} // namespace Raven
