#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodyCloth.h"
#include "Raven/Physics/SoftBody/SoftBodyParticleTriangleSelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

namespace Raven::ph::tests
{
namespace
{

bool NearlyEqual(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}

} // namespace

// ============================================================================
// Particle-Triangle Self Collision Self Tests
// ============================================================================
// assertベースで手動実行できる回帰テストです。
void RunSoftBodyParticleTriangleSelfCollisionSelfTests()
{
    // ------------------------------------------------------------------------
    // 1. Triangle AABB Broad Phase
    // ------------------------------------------------------------------------
    {
        std::vector<SoftBodyParticle> particles(4u);
        particles[0].Position = { 0.0f, 0.0f, 0.0f };
        particles[1].Position = { 1.0f, 0.0f, 0.0f };
        particles[2].Position = { 0.0f, 1.0f, 0.0f };
        particles[3].Position = { 0.25f, 0.25f, 0.05f };

        std::vector<SoftBodyTriangle> triangles;
        triangles.push_back({ 0u, 1u, 2u });

        SoftBodyTriangleSpatialHashGrid grid(0.25f);
        grid.BuildTriangles(particles, triangles, 0.10f);

        std::vector<SoftBodyParticleTrianglePair> pairs;
        grid.GenerateParticleTriangleCandidates(particles, pairs);

        bool foundParticle3Triangle0 = false;
        for (const SoftBodyParticleTrianglePair& pair : pairs)
        {
            if (pair.ParticleIndex == 3u && pair.TriangleIndex == 0u)
            {
                foundParticle3Triangle0 = true;
            }
        }
        assert(foundParticle3Triangle0);
    }

    // ------------------------------------------------------------------------
    // 1-2. Generation切替後に前Buildの古いCellを候補へ混ぜない
    // ------------------------------------------------------------------------
    {
        std::vector<SoftBodyParticle> particles(4u);
        particles[0].Position = { 0.0f, 0.0f, 0.0f };
        particles[1].Position = { 1.0f, 0.0f, 0.0f };
        particles[2].Position = { 0.0f, 1.0f, 0.0f };
        particles[3].Position = { 0.25f, 0.25f, 0.05f };

        std::vector<SoftBodyTriangle> triangles;
        triangles.push_back({ 0u, 1u, 2u });

        SoftBodyTriangleSpatialHashGrid grid(0.25f);
        std::vector<SoftBodyParticleTrianglePair> pairs;

        // 1回目はParticle 3とTriangle 0が同じBroad Phase領域に存在します。
        grid.BuildTriangles(particles, triangles, 0.10f);
        grid.GenerateParticleTriangleCandidates(particles, pairs);

        bool foundBeforeMove = false;
        for (const SoftBodyParticleTrianglePair& pair : pairs)
        {
            if (pair.ParticleIndex == 3u && pair.TriangleIndex == 0u)
            {
                foundBeforeMove = true;
            }
        }
        assert(foundBeforeMove);

        // Triangleだけを大きく移動します。
        // Generation方式では旧Cell node/vectorを再利用のためMapへ保持しますが、
        // 現Generationで触られていない旧Cellは候補として参照されてはいけません。
        particles[0].Position.x += 10.0f;
        particles[1].Position.x += 10.0f;
        particles[2].Position.x += 10.0f;

        grid.BuildTriangles(particles, triangles, 0.10f);
        grid.GenerateParticleTriangleCandidates(particles, pairs);

        bool foundAfterMove = false;
        for (const SoftBodyParticleTrianglePair& pair : pairs)
        {
            if (pair.ParticleIndex == 3u && pair.TriangleIndex == 0u)
            {
                foundAfterMove = true;
            }
        }
        assert(foundAfterMove == false);

        // 元のCellへ戻したときは、保持していたBucket capacityを再利用しつつ
        // 現Generationの正しい候補として再び検出できることを確認します。
        particles[0].Position.x -= 10.0f;
        particles[1].Position.x -= 10.0f;
        particles[2].Position.x -= 10.0f;

        grid.BuildTriangles(particles, triangles, 0.10f);
        grid.GenerateParticleTriangleCandidates(particles, pairs);

        bool foundAfterReturn = false;
        for (const SoftBodyParticleTrianglePair& pair : pairs)
        {
            if (pair.ParticleIndex == 3u && pair.TriangleIndex == 0u)
            {
                foundAfterReturn = true;
            }
        }
        assert(foundAfterReturn);
    }

    // ------------------------------------------------------------------------
    // 2. 非隣接Particleが別Triangle面からThickness外へ押し出される
    // ------------------------------------------------------------------------
    {
        SoftBodySolver solver;
        solver.SetGravity({ 0.0f, 0.0f, 0.0f });

        SoftBodyClothSettings clothSettings{};
        clothSettings.Rows = 2u;
        clothSettings.Columns = 2u;
        clothSettings.Width = 2.0f;
        clothSettings.Height = 2.0f;
        clothSettings.PinTopLeft = false;
        clothSettings.PinTopRight = false;
        clothSettings.BendingModel = SoftBodyClothBendingModel::Dihedral;

        SoftBodyCloth cloth = SoftBodyClothBuilder::Build(solver, clothSettings);
        std::vector<SoftBodyParticle>& particles = solver.GetParticles();

        // Particle0は左上頂点です。右下QuadのT1=(center,bottomRight,middleRight)には含まれません。
        const uint32_t movingParticleIndex = cloth.GetParticleIndex(0u, 0u);
        const uint32_t triangleAIndex = cloth.GetParticleIndex(1u, 1u);
        const uint32_t triangleBIndex = cloth.GetParticleIndex(2u, 2u);
        const uint32_t triangleCIndex = cloth.GetParticleIndex(1u, 2u);

        const math::Vec3 triangleCenter =
            (particles[triangleAIndex].Position
                + particles[triangleBIndex].Position
                + particles[triangleCIndex].Position) / 3.0f;

        particles[movingParticleIndex].PreviousPosition = triangleCenter + math::Vec3{ 0.0f, 0.0f, 0.02f };
        particles[movingParticleIndex].Position = triangleCenter + math::Vec3{ 0.0f, 0.0f, 0.02f };

        const math::Vec3 triangleABefore = particles[triangleAIndex].Position;
        const math::Vec3 triangleBBefore = particles[triangleBIndex].Position;
        const math::Vec3 triangleCBefore = particles[triangleCIndex].Position;

        SoftBodyParticleTriangleSelfCollisionSettings settings{};
        settings.Enabled = true;
        settings.Thickness = 0.10f;
        settings.SolverIterations = 4u;
        settings.Compliance = 0.0f;

        SolveSoftBodyParticleTriangleSelfCollisions(
            solver,
            cloth,
            1.0f / 60.0f,
            settings);

        // Particleは元のz=0.02より面から離れる方向へ押されます。
        assert(particles[movingParticleIndex].Position.z > 0.02f);

        // Triangle側にもBarycentric Weightで反作用が入るため、少なくとも1頂点は-z側へ動きます。
        const bool triangleMoved =
            particles[triangleAIndex].Position.z < triangleABefore.z
            || particles[triangleBIndex].Position.z < triangleBBefore.z
            || particles[triangleCIndex].Position.z < triangleCBefore.z;
        assert(triangleMoved);
    }

    // ------------------------------------------------------------------------
    // 3. 自分を含むTriangleは除外される
    // ------------------------------------------------------------------------
    {
        SoftBodySolver solver;
        solver.SetGravity({ 0.0f, 0.0f, 0.0f });

        SoftBodyClothSettings clothSettings{};
        clothSettings.Rows = 1u;
        clothSettings.Columns = 1u;
        clothSettings.Width = 1.0f;
        clothSettings.Height = 1.0f;
        clothSettings.PinTopLeft = false;
        clothSettings.PinTopRight = false;

        SoftBodyCloth cloth = SoftBodyClothBuilder::Build(solver, clothSettings);
        std::vector<SoftBodyParticle>& particles = solver.GetParticles();

        std::vector<math::Vec3> beforePositions;
        beforePositions.reserve(particles.size());
        for (const SoftBodyParticle& particle : particles)
        {
            beforePositions.push_back(particle.Position);
        }

        SoftBodyParticleTriangleSelfCollisionSettings settings{};
        settings.Enabled = true;
        settings.Thickness = 0.05f;
        settings.SolverIterations = 1u;
        settings.Compliance = 0.0f;

        SolveSoftBodyParticleTriangleSelfCollisions(
            solver,
            cloth,
            1.0f / 60.0f,
            settings);

        // 1x1の平坦なClothで各頂点はincident Triangle上にありますが、それらはTopologyとして除外されます。
        // Thicknessだけを理由に自分自身から押し出されてはいけません。
        for (std::size_t index = 0u; index < particles.size(); ++index)
        {
            assert(NearlyEqual(particles[index].Position.x, beforePositions[index].x));
            assert(NearlyEqual(particles[index].Position.y, beforePositions[index].y));
            assert(NearlyEqual(particles[index].Position.z, beforePositions[index].z));
        }
    }
}

} // namespace Raven::ph::tests
