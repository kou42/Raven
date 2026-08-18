#include <cassert>
#include <cmath>

#include "Raven/Physics/SoftBody/SoftBodyJelly.h"
#include "Raven/Physics/SoftBody/XPBDVolumeConstraint.h"

namespace Raven::ph::tests
{
namespace
{

float ComputeTotalSignedVolume(
    const SoftBodySolver& solver,
    const SoftBodyJelly& jelly)
{
    float totalVolume = 0.0f;
    const std::vector<SoftBodyParticle>& particles = solver.GetParticles();

    for (const SoftBodyTetrahedron& tetrahedron : jelly.Tetrahedra)
    {
        totalVolume += ComputeSignedTetrahedronVolume(
            particles[tetrahedron.Particle0].Position,
            particles[tetrahedron.Particle1].Position,
            particles[tetrahedron.Particle2].Position,
            particles[tetrahedron.Particle3].Position);
    }

    return totalVolume;
}

} // namespace

// ============================================================================
// Soft Body Jelly Builder Self Tests
// ============================================================================
// 規則格子 -> 6-Tet分割 -> Distance + Volume Constraint登録が期待したTopologyになり、
// 圧縮したCubeがVolume Constraintによって体積を回復することをassertで確認します。
void RunSoftBodyJellySelfTests()
{
    SoftBodySolver solver{};
    solver.SetGravity({ 0.0f, 0.0f, 0.0f });

    SoftBodySolverSettings solverSettings{};
    solverSettings.SolverIterations = 16u;
    solverSettings.CollisionThickness = 0.0f;
    solver.SetSettings(solverSettings);

    SoftBodyJellySettings jellySettings{};
    jellySettings.CellsX = 1u;
    jellySettings.CellsY = 1u;
    jellySettings.CellsZ = 1u;
    jellySettings.Width = 1.0f;
    jellySettings.Height = 1.0f;
    jellySettings.Depth = 1.0f;
    jellySettings.InverseMass = 1.0f;
    jellySettings.DistanceCompliance = 0.0001f;
    jellySettings.VolumeCompliance = 0.0f;
    jellySettings.VelocityDamping = 0.0f;

    SoftBodyJelly jelly = SoftBodyJellyBuilder::Build(solver, jellySettings);

    // 1 Cube Cellには8頂点があり、000->111体対角線を共有する6 Tetへ分割します。
    assert(jelly.ParticleIndices.size() == 8u);
    assert(jelly.Tetrahedra.size() == 6u);
    assert(jelly.VolumeConstraints.size() == 6u);

    // 6個の四面体が持つ全Edgeを重複排除すると19本です。
    // 共有Edgeを重複登録していないことをこの数で確認します。
    assert(solver.GetDistanceConstraints().size() == 19u);

    // 単位Cubeを6 Tetへ分けているので、符号付きRestVolumeの総和は1.0になります。
    float totalRestVolume = 0.0f;
    for (const XPBDVolumeConstraint& constraint : jelly.VolumeConstraints)
    {
        assert(constraint.RestVolume > 0.0f);
        totalRestVolume += constraint.RestVolume;
    }
    assert(std::abs(totalRestVolume - 1.0f) < 0.0001f);

    // ========================================================================
    // Compression Recovery
    // ========================================================================
    // 上面(y=+0.5)を中央近くまで押し下げ、Cube全体を強く圧縮します。
    // Volume Constraintが無ければこの薄い状態を許せますが、Volume Constraintを解くと
    // 各TetがRestVolumeへ戻ろうとするため総体積が明確に回復するはずです。
    std::vector<SoftBodyParticle>& particles = solver.GetParticles();
    for (uint32_t z = 0u; z <= jelly.CellsZ; ++z)
    {
        for (uint32_t x = 0u; x <= jelly.CellsX; ++x)
        {
            const uint32_t topParticle = jelly.GetParticleIndex(x, jelly.CellsY, z);
            particles[topParticle].Position.y = -0.25f;
            particles[topParticle].PreviousPosition = particles[topParticle].Position;
            particles[topParticle].Velocity = math::Vec3{};
        }
    }

    const float compressedVolume = ComputeTotalSignedVolume(solver, jelly);
    assert(compressedVolume < 0.5f);

    StepSoftBodyJelly(solver, jelly, 1.0f / 60.0f);

    const float recoveredVolume = ComputeTotalSignedVolume(solver, jelly);

    // Coupled Constraintなので1 Stepで数学的に完全な1.0を要求せず、
    // 圧縮状態から十分大きく回復していることを回帰条件にします。
    assert(recoveredVolume > compressedVolume + 0.25f);
    assert(recoveredVolume > 0.8f);

    // ========================================================================
    // Velocity Damping
    // ========================================================================
    // 全Particleへ同じVelocityを与える一様並進では内部Constraint Errorが発生しません。
    // したがって60Hz・damping=0.5ならStep後速度が正確に半分になることを確認できます。
    SoftBodySolver dampingSolver{};
    dampingSolver.SetGravity({ 0.0f, 0.0f, 0.0f });
    dampingSolver.SetSettings(solverSettings);

    SoftBodyJellySettings dampingSettings = jellySettings;
    dampingSettings.DistanceCompliance = 0.0f;
    dampingSettings.VolumeCompliance = 0.0f;
    dampingSettings.VelocityDamping = 0.5f;

    SoftBodyJelly dampingJelly = SoftBodyJellyBuilder::Build(dampingSolver, dampingSettings);
    for (SoftBodyParticle& particle : dampingSolver.GetParticles())
    {
        particle.Velocity = { 2.0f, 0.0f, 0.0f };
    }

    StepSoftBodyJelly(dampingSolver, dampingJelly, 1.0f / 60.0f);

    for (const SoftBodyParticle& particle : dampingSolver.GetParticles())
    {
        assert(std::abs(particle.Velocity.x - 1.0f) < 0.0001f);
        assert(std::abs(particle.Velocity.y) < 0.0001f);
        assert(std::abs(particle.Velocity.z) < 0.0001f);
    }
}

} // namespace Raven::ph::tests
