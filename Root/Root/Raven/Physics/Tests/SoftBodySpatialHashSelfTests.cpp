#include <algorithm>
#include <cassert>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"

namespace Raven::ph::tests
{

namespace
{

bool ContainsPair(
    const std::vector<SoftBodySpatialHashPair>& pairs,
    uint32_t particleA,
    uint32_t particleB)
{
    if (particleA > particleB)
    {
        std::swap(particleA, particleB);
    }

    for (const SoftBodySpatialHashPair& pair : pairs)
    {
        if (pair.ParticleA == particleA && pair.ParticleB == particleB)
        {
            return true;
        }
    }

    return false;
}

} // namespace

// ============================================================================
// Soft Body Spatial Hash Self Tests
// ============================================================================
// assertベースでBroad Phase単体を確認する回帰テストです。
// Narrow Phaseをまだ実装していない段階でも、セル境界・負座標・Pair重複除去という
// Spatial Hashで壊れやすい部分を独立して検証できます。
void RunSoftBodySpatialHashSelfTests()
{
    SoftBodySpatialHashGrid grid(1.0f);
    std::vector<SoftBodyParticle> particles(5u);

    // Particle 0/1は同一セル、2は隣接セル、3は十分遠いセルです。
    // Particle 4は負座標側へ置き、floorによるセル化も確認します。
    particles[0].Position = { 0.10f, 0.10f, 0.10f };
    particles[1].Position = { 0.90f, 0.20f, 0.10f };
    particles[2].Position = { 1.10f, 0.10f, 0.10f };
    particles[3].Position = { 4.00f, 0.00f, 0.00f };
    particles[4].Position = { -0.10f, 0.10f, 0.10f };

    grid.Build(particles);

    assert(grid.GetParticleCount() == 5u);
    assert(grid.GetOccupiedCellCount() == 4u);

    std::vector<SoftBodySpatialHashPair> pairs;
    grid.GenerateCandidatePairs(pairs);

    assert(ContainsPair(pairs, 0u, 1u));
    assert(ContainsPair(pairs, 0u, 2u));
    assert(ContainsPair(pairs, 1u, 2u));
    assert(ContainsPair(pairs, 0u, 4u));
    assert(ContainsPair(pairs, 1u, 4u));

    // 3は中心セルから3セル以上離れているため候補になりません。
    assert(ContainsPair(pairs, 0u, 3u) == false);
    assert(ContainsPair(pairs, 2u, 3u) == false);

    // Broad Phase Pairは必ずA < Bで、一度だけ出現することを確認します。
    for (std::size_t pairIndex = 0u; pairIndex < pairs.size(); ++pairIndex)
    {
        assert(pairs[pairIndex].ParticleA < pairs[pairIndex].ParticleB);

        for (std::size_t otherIndex = pairIndex + 1u; otherIndex < pairs.size(); ++otherIndex)
        {
            const bool samePair =
                pairs[pairIndex].ParticleA == pairs[otherIndex].ParticleA
                && pairs[pairIndex].ParticleB == pairs[otherIndex].ParticleB;
            assert(samePair == false);
        }
    }

    // CellSize変更時は古いGridをClearします。
    grid.SetCellSize(0.5f);
    assert(grid.GetParticleCount() == 0u);
    assert(grid.GetOccupiedCellCount() == 0u);
}

} // namespace Raven::ph::tests
