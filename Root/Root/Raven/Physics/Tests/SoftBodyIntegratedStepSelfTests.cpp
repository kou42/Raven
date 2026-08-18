#include <cassert>

#include "Raven/Physics/SoftBody/SoftBodyCloth.h"
#include "Raven/Physics/SoftBody/SoftBodyParticleTriangleSelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven::ph::tests
{

// ============================================================================
// Integrated Soft Body Step Self Tests
// ============================================================================
// Cloth自己衝突がSolver::Step()後の別passではなく、Internal / External Constraintと
// 同じSolverIterations内で解かれることを確認するためのassertベース回帰テストです。
void RunSoftBodyIntegratedStepSelfTests()
{
    SoftBodySolver solver{};
    solver.SetGravity({ 0.0f, 0.0f, 0.0f });

    SoftBodySolverSettings solverSettings{};
    solverSettings.SolverIterations = 6u;
    solverSettings.CollisionThickness = 0.0f;
    solver.SetSettings(solverSettings);

    SoftBodyClothSettings clothSettings{};
    clothSettings.Rows = 1u;
    clothSettings.Columns = 1u;
    clothSettings.Width = 1.0f;
    clothSettings.Height = 1.0f;
    clothSettings.InverseMass = 1.0f;
    clothSettings.StructuralCompliance = 0.0f;
    clothSettings.ShearCompliance = 0.0f;
    clothSettings.BendingModel = SoftBodyClothBendingModel::Dihedral;
    clothSettings.PinTopLeft = false;
    clothSettings.PinTopRight = false;

    SoftBodyCloth cloth = SoftBodyClothBuilder::Build(solver, clothSettings);

    // 1x1 Clothの4 Particleのうち、TRを反対Triangleへ近づけます。
    // 自分自身を含むTriangleは除外されるため、ここでは追加Particleを1つ用意して
    // Cloth面中央へ近づけ、Particle-Triangle自己衝突の統合処理を確認します。
    const uint32_t probe = solver.AddParticle({ 0.0f, 0.0f, 0.001f }, 1.0f);
    assert(probe < solver.GetParticles().size());

    SoftBodySelfCollisionSettings particleSettings{};
    particleSettings.Enabled = true;
    particleSettings.ParticleRadius = 0.02f;
    particleSettings.Compliance = 0.0f;

    SoftBodyParticleTriangleSelfCollisionSettings particleTriangleSettings{};
    particleTriangleSettings.Enabled = true;
    particleTriangleSettings.Thickness = 0.05f;
    particleTriangleSettings.Compliance = 0.0f;

    solver.StepWithSelfCollisions(
        1.0f / 60.0f,
        cloth,
        particleSettings,
        particleTriangleSettings);

    const SoftBodyParticle& probeParticle = solver.GetParticles()[probe];

    // Cloth初期面はz=0なので、probeは少なくともThickness方向へ押し戻されます。
    // Triangle側も可動なので厳密に0.05固定ではありませんが、初期0.001より明確に離れることを確認します。
    assert(probeParticle.Position.z > 0.001f);

    // 統合Stepの最後でVelocityを1回だけ再構築するため、自己衝突による移動がVelocityにも反映されます。
    assert(probeParticle.Velocity.z > 0.0f);
}

} // namespace Raven::ph::tests
