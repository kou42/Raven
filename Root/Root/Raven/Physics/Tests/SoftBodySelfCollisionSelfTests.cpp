#include <cassert>
#include <cmath>

#include "Raven/Physics/SoftBody/SoftBodySelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"

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
// Soft Body Particle Self Collision Self Tests
// ============================================================================
// Spatial Hash の候補生成そのものは SoftBodySpatialHashSelfTests で確認し、ここでは
// Narrow Phase / XPBD 補正 / Topology 除外 / 固定点の質量分配を重点的に確認します。
void RunSoftBodySelfCollisionSelfTests()
{
    SoftBodySelfCollisionSettings selfCollisionSettings{};
    selfCollisionSettings.Enabled = true;
    selfCollisionSettings.ParticleRadius = 0.1f;
    selfCollisionSettings.SolverIterations = 4u;
    selfCollisionSettings.Compliance = 0.0f;

    // ------------------------------------------------------------------------
    // 1. 非隣接 Particle 同士を直径まで押し離す
    // ------------------------------------------------------------------------
    {
        SoftBodySolver solver{};
        solver.SetGravity({ 0.0f, 0.0f, 0.0f });

        solver.AddParticle({ -0.025f, 0.0f, 0.0f }, 1.0f);
        solver.AddParticle({  0.025f, 0.0f, 0.0f }, 1.0f);

        const float deltaTime = 1.0f / 60.0f;
        solver.Step(deltaTime);
        SolveSoftBodyParticleSelfCollisions(solver, deltaTime, selfCollisionSettings);

        const std::vector<SoftBodyParticle>& particles = solver.GetParticles();
        const float distance = (particles[1].Position - particles[0].Position).Length();
        assert(NearlyEqual(distance, 0.2f, 1.0e-3f));
    }

    // ------------------------------------------------------------------------
    // 2. Distance Constraint で直接接続された Pair は除外する
    // ------------------------------------------------------------------------
    {
        SoftBodySolver solver{};
        solver.SetGravity({ 0.0f, 0.0f, 0.0f });

        const uint32_t particleA = solver.AddParticle({ 0.0f, 0.0f, 0.0f }, 1.0f);
        const uint32_t particleB = solver.AddParticle({ 0.05f, 0.0f, 0.0f }, 1.0f);
        solver.AddDistanceConstraint(particleA, particleB, 0.0f);

        const float deltaTime = 1.0f / 60.0f;
        solver.Step(deltaTime);
        SolveSoftBodyParticleSelfCollisions(solver, deltaTime, selfCollisionSettings);

        const std::vector<SoftBodyParticle>& particles = solver.GetParticles();
        const float distance = (particles[1].Position - particles[0].Position).Length();

        // Self Collision Diameter は 0.2 ですが、Topology 隣接 Pair なので自然長 0.05 を維持します。
        assert(NearlyEqual(distance, 0.05f, 1.0e-3f));
    }

    // ------------------------------------------------------------------------
    // 3. 固定点との衝突では可動 Particle 側だけを補正する
    // ------------------------------------------------------------------------
    {
        SoftBodySolver solver{};
        solver.SetGravity({ 0.0f, 0.0f, 0.0f });

        solver.AddParticle({ 0.0f, 0.0f, 0.0f }, 0.0f);
        solver.AddParticle({ 0.05f, 0.0f, 0.0f }, 1.0f);

        const float deltaTime = 1.0f / 60.0f;
        solver.Step(deltaTime);
        SolveSoftBodyParticleSelfCollisions(solver, deltaTime, selfCollisionSettings);

        const std::vector<SoftBodyParticle>& particles = solver.GetParticles();
        assert(NearlyEqual(particles[0].Position.x, 0.0f));
        assert(NearlyEqual(particles[1].Position.x, 0.2f, 1.0e-3f));
    }

    // ------------------------------------------------------------------------
    // 4. 無効設定では Position を変更しない
    // ------------------------------------------------------------------------
    {
        SoftBodySolver solver{};
        solver.SetGravity({ 0.0f, 0.0f, 0.0f });
        solver.AddParticle({ 0.0f, 0.0f, 0.0f }, 1.0f);
        solver.AddParticle({ 0.05f, 0.0f, 0.0f }, 1.0f);

        SoftBodySelfCollisionSettings disabledSettings = selfCollisionSettings;
        disabledSettings.Enabled = false;

        const float deltaTime = 1.0f / 60.0f;
        solver.Step(deltaTime);
        SolveSoftBodyParticleSelfCollisions(solver, deltaTime, disabledSettings);

        const std::vector<SoftBodyParticle>& particles = solver.GetParticles();
        assert(NearlyEqual(particles[0].Position.x, 0.0f));
        assert(NearlyEqual(particles[1].Position.x, 0.05f));
    }
}

} // namespace Raven::ph::tests
