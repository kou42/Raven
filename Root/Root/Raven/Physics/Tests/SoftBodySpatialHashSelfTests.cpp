#include <algorithm>
#include <cassert>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

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

bool ContainsParticleTrianglePair(
    const std::vector<SoftBodyParticleTrianglePair>& pairs,
    uint32_t particleIndex,
    uint32_t triangleIndex)
{
    for (const SoftBodyParticleTrianglePair& pair : pairs)
    {
        if (pair.ParticleIndex == particleIndex
            && pair.TriangleIndex == triangleIndex)
        {
            return true;
        }
    }

    return false;
}

void AssertUniqueNormalizedPairs(const std::vector<SoftBodySpatialHashPair>& pairs)
{
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
    // ------------------------------------------------------------------------
    // 1. Same Cell / Axis Neighbor / Negative Coordinate
    // ------------------------------------------------------------------------
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

        AssertUniqueNormalizedPairs(pairs);

        // CellSize変更時は古いGridをClearします。
        grid.SetCellSize(0.5f);
        assert(grid.GetParticleCount() == 0u);
        assert(grid.GetOccupiedCellCount() == 0u);
    }

    // ------------------------------------------------------------------------
    // 2. Diagonal 3D Neighbor
    // ------------------------------------------------------------------------
    // Candidate生成を13方向だけへ半減したため、軸方向だけでなく対角Neighborも
    // 取りこぼさないことを明示的に確認します。
    {
        SoftBodySpatialHashGrid grid(1.0f);
        std::vector<SoftBodyParticle> particles(4u);

        particles[0].Position = { 0.10f, 0.10f, 0.10f };   // Cell (0, 0, 0)
        particles[1].Position = { 1.10f, 1.10f, 1.10f };   // Cell (1, 1, 1)
        particles[2].Position = { -0.10f, -0.10f, 1.10f }; // Cell (-1, -1, 1)
        particles[3].Position = { 2.10f, 2.10f, 2.10f };   // Cell (2, 2, 2)

        grid.Build(particles);

        std::vector<SoftBodySpatialHashPair> pairs;
        grid.GenerateCandidatePairs(pairs);

        // 26近傍の角に位置するCell同士も候補になります。
        assert(ContainsPair(pairs, 0u, 1u));
        assert(ContainsPair(pairs, 0u, 2u));

        // Cell差が各軸2のParticle 3はParticle 0の近傍ではありません。
        assert(ContainsPair(pairs, 0u, 3u) == false);

        AssertUniqueNormalizedPairs(pairs);
    }

    // ------------------------------------------------------------------------
    // 3. Build Reuse
    // ------------------------------------------------------------------------
    // XPBDでは同じGrid instanceをiterationごとにBuildし直すため、前BuildのCellが残らず
    // 新しい配置だけから候補が生成されることを確認します。
    {
        SoftBodySpatialHashGrid grid(1.0f);
        std::vector<SoftBodyParticle> particles(2u);
        std::vector<SoftBodySpatialHashPair> pairs;

        particles[0].Position = { 0.10f, 0.0f, 0.0f };
        particles[1].Position = { 0.20f, 0.0f, 0.0f };
        grid.Build(particles);
        grid.GenerateCandidatePairs(pairs);
        assert(ContainsPair(pairs, 0u, 1u));

        particles[1].Position = { 10.0f, 0.0f, 0.0f };
        grid.Build(particles);
        grid.GenerateCandidatePairs(pairs);
        assert(ContainsPair(pairs, 0u, 1u) == false);
    }

    // ------------------------------------------------------------------------
    // 4. Particle-Triangle Expanded AABB Early Reject
    // ------------------------------------------------------------------------
    // Triangle AABBがCell (0,0,0)へ登録されていても、そのCell全体がTriangleの
    // Thickness込みAABB内とは限りません。同じCellにいるだけの不要候補をClosest Point計算へ
    // 流さないことと、実際に膨張AABB内にいるParticleは候補として保持することを確認します。
    {
        SoftBodyTriangleSpatialHashGrid grid(1.0f);
        std::vector<SoftBodyParticle> particles(5u);

        // Triangleは原点近傍の小さな形状です。expansion=0.1なので膨張AABBは概ね
        // x/y=[0.0,0.3], z=[-0.1,0.1]になります。
        particles[0].Position = { 0.10f, 0.10f, 0.0f };
        particles[1].Position = { 0.20f, 0.10f, 0.0f };
        particles[2].Position = { 0.10f, 0.20f, 0.0f };

        // Particle 3は同じCell (0,0,0)ですが膨張AABB外です。
        // 旧Cell単位候補では残りますが、Exact AABB Early Reject後は除外されます。
        particles[3].Position = { 0.90f, 0.90f, 0.90f };

        // Particle 4は膨張AABB内かつPlane距離もThickness以内なので候補を維持する必要があります。
        particles[4].Position = { 0.15f, 0.15f, 0.05f };

        std::vector<SoftBodyTriangle> triangles;
        triangles.push_back({ 0u, 1u, 2u });

        grid.BuildTriangles(particles, triangles, 0.10f);

        std::vector<SoftBodyParticleTrianglePair> pairs;
        grid.GenerateParticleTriangleCandidates(particles, pairs);

        assert(ContainsParticleTrianglePair(pairs, 3u, 0u) == false);
        assert(ContainsParticleTrianglePair(pairs, 4u, 0u));
    }

    // ------------------------------------------------------------------------
    // 5. Particle-Triangle Plane Distance Early Reject
    // ------------------------------------------------------------------------
    // Expanded AABBは軸平行Boxなので、斜めTriangleではAABB内部にもTriangle Planeから
    // Thickness以上離れた領域が残ります。その領域をClosest Point計算前に除外できることを確認します。
    {
        SoftBodyTriangleSpatialHashGrid grid(1.0f);
        std::vector<SoftBodyParticle> particles(5u);

        // Planeは z = x + y となる斜めTriangleです。
        // expansion=0.1のExpanded AABBは各軸[-0.1, 0.3]程度まで広がります。
        particles[0].Position = { 0.0f, 0.0f, 0.0f };
        particles[1].Position = { 0.2f, 0.0f, 0.2f };
        particles[2].Position = { 0.0f, 0.2f, 0.2f };

        // Particle 3はExpanded AABB内ですが、Plane z=x+y までの距離は約0.202です。
        // Thickness=0.1より大きいためPlane Early Rejectで除外される必要があります。
        particles[3].Position = { -0.05f, -0.05f, 0.25f };

        // Particle 4もExpanded AABB内ですがPlane距離は約0.029なので候補を維持します。
        particles[4].Position = { 0.10f, 0.10f, 0.25f };

        std::vector<SoftBodyTriangle> triangles;
        triangles.push_back({ 0u, 1u, 2u });

        grid.BuildTriangles(particles, triangles, 0.10f);

        std::vector<SoftBodyParticleTrianglePair> pairs;
        grid.GenerateParticleTriangleCandidates(particles, pairs);

        assert(ContainsParticleTrianglePair(pairs, 3u, 0u) == false);
        assert(ContainsParticleTrianglePair(pairs, 4u, 0u));
    }

    // ------------------------------------------------------------------------
    // 6. Detailed Profilingは候補集合へ影響しない
    // ------------------------------------------------------------------------
    // 通常経路では診断専用Metrics / Timer / Counterを停止しますが、AABB・Plane・Edge判定は
    // 常に実行されます。診断ON/OFFで候補の内容と順序が完全一致することを固定します。
    {
        std::vector<SoftBodyParticle> particles(5u);
        particles[0].Position = { 0.0f, 0.0f, 0.0f };
        particles[1].Position = { 0.2f, 0.0f, 0.0f };
        particles[2].Position = { 0.0f, 0.2f, 0.0f };
        particles[3].Position = { 0.05f, 0.05f, 0.02f };
        particles[4].Position = { 0.8f, 0.8f, 0.8f };

        std::vector<SoftBodyTriangle> triangles;
        triangles.push_back({ 0u, 1u, 2u });

        SoftBodyTriangleSpatialHashGrid grid(0.25f);
        std::vector<SoftBodyParticleTrianglePair> normalPairs;
        std::vector<SoftBodyParticleTrianglePair> detailedPairs;

        grid.SetDetailedProfilingEnabled(false);
        grid.BuildTriangles(particles, triangles, 0.05f);
        grid.GenerateParticleTriangleCandidates(particles, normalPairs);

        grid.SetDetailedProfilingEnabled(true);
        grid.BuildTriangles(particles, triangles, 0.05f);
        grid.GenerateParticleTriangleCandidates(particles, detailedPairs);

        assert(normalPairs.size() == detailedPairs.size());
        for (std::size_t pairIndex = 0u; pairIndex < normalPairs.size(); ++pairIndex)
        {
            assert(normalPairs[pairIndex].ParticleIndex == detailedPairs[pairIndex].ParticleIndex);
            assert(normalPairs[pairIndex].TriangleIndex == detailedPairs[pairIndex].TriangleIndex);
        }
    }
}

} // namespace Raven::ph::tests
