#pragma once

#include <vector>

#include "Raven/Physics/Particle/ParticleSpatialHashGrid.h"
#include "Raven/Physics/Solver/SolverTemporaryAllocationCounter.h"
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
// Spatial Hash本体はParticleSpatialHashGridへ集約し、このClassはSoftBodyParticleから
// Positionを取り出してCoreへ登録する薄いAdapterとして維持します。
// これにより既存Self Collision側のAPIを変更せず、Fluidなど別Particle系Simulationでも
// 同じHash実装を再利用できます。
class SoftBodySpatialHashGrid : public ParticleSpatialHashGrid
{
public:
    explicit SoftBodySpatialHashGrid(float cellSize = 0.05f);

    // Base Classの通常vector版を、下記Temporary Allocator版で隠さないよう公開します。
    using ParticleSpatialHashGrid::GenerateCandidatePairs;

    // 現在のSoftBody Particle PositionからGridを再構築します。
    void Build(const std::vector<SoftBodyParticle>& particles);

    // Temporary allocation計測用Overloadです。
    // 第一段階では既存の計測経路を維持し、性能比較結果を変えないようにします。
    void GenerateCandidatePairs(
        std::vector<
            SoftBodySpatialHashPair,
            SolverTemporaryAllocator<SoftBodySpatialHashPair>>& outPairs) const;
};

} // namespace ph
} // namespace Raven
