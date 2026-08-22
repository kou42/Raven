#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Physics/SoftBody/XPBDVolumeConstraint.h"

namespace Raven::ph::tests
{

namespace
{

bool NearlyEqual(float a, float b, float tolerance = 1.0e-4f)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

// ============================================================================
// XPBD Volume Constraint Self Tests
// ============================================================================
// ゼリー実装の最小単位である「1四面体」の体積保存を確認するassertベース回帰テストです。
// Cube Jellyへ進む前に、体積式・Gradient・符号・XPBD Lambda更新が正しいことを独立して検証します。
void RunSoftBodyVolumeConstraintSelfTests()
{
    SoftBodySolver solver{};
    solver.SetGravity({ 0.0f, 0.0f, 0.0f });

    SoftBodySolverSettings settings{};
    settings.SolverIterations = 8u;
    settings.CollisionThickness = 0.0f;
    solver.SetSettings(settings);

    // p0-p1-p2を固定した底面、p3を可動頂点にした単純な四面体です。
    // 初期符号付き体積は 1 / 6 になります。
    const uint32_t p0 = solver.AddParticle({ 0.0f, 0.0f, 0.0f }, 0.0f);
    const uint32_t p1 = solver.AddParticle({ 1.0f, 0.0f, 0.0f }, 0.0f);
    const uint32_t p2 = solver.AddParticle({ 0.0f, 1.0f, 0.0f }, 0.0f);
    const uint32_t p3 = solver.AddParticle({ 0.0f, 0.0f, 1.0f }, 1.0f);

    std::vector<XPBDVolumeConstraint> volumeConstraints;
    volumeConstraints.push_back(CreateXPBDVolumeConstraint(
        solver.GetParticles(),
        p0,
        p1,
        p2,
        p3,
        0.0f));

    assert(volumeConstraints.size() == 1u);
    assert(NearlyEqual(volumeConstraints[0].RestVolume, 1.0f / 6.0f));

    // ------------------------------------------------------------------------
    // Compression test
    // ------------------------------------------------------------------------
    // 可動頂点を底面側へ押し込み、体積を1/4まで潰します。
    // hard constraint(compliance=0)なので、反復後はRestVolumeへ十分近く戻る必要があります。
    solver.GetParticles()[p3].Position = { 0.0f, 0.0f, 0.25f };
    solver.GetParticles()[p3].PreviousPosition = solver.GetParticles()[p3].Position;
    solver.GetParticles()[p3].Velocity = {};

    const float compressedVolume = ComputeSignedTetrahedronVolume(
        solver.GetParticles()[p0].Position,
        solver.GetParticles()[p1].Position,
        solver.GetParticles()[p2].Position,
        solver.GetParticles()[p3].Position);
    assert(compressedVolume < volumeConstraints[0].RestVolume);

    solver.StepWithVolumeConstraints(1.0f / 60.0f, volumeConstraints);

    const float restoredVolume = ComputeSignedTetrahedronVolume(
        solver.GetParticles()[p0].Position,
        solver.GetParticles()[p1].Position,
        solver.GetParticles()[p2].Position,
        solver.GetParticles()[p3].Position);
    assert(NearlyEqual(restoredVolume, volumeConstraints[0].RestVolume));

    // ------------------------------------------------------------------------
    // Inversion recovery test
    // ------------------------------------------------------------------------
    // p3を底面の反対側(z < 0)へ移動すると符号付き体積は負になります。
    // RestVolumeをabs()で保存している実装では裏返りを正しく扱えないため、
    // このテストで「符号を保持したConstraint」が元の向きへ戻すことを確認します。
    solver.GetParticles()[p3].Position = { 0.0f, 0.0f, -0.25f };
    solver.GetParticles()[p3].PreviousPosition = solver.GetParticles()[p3].Position;
    solver.GetParticles()[p3].Velocity = {};

    const float invertedVolume = ComputeSignedTetrahedronVolume(
        solver.GetParticles()[p0].Position,
        solver.GetParticles()[p1].Position,
        solver.GetParticles()[p2].Position,
        solver.GetParticles()[p3].Position);
    assert(invertedVolume < 0.0f);

    solver.StepWithVolumeConstraints(1.0f / 60.0f, volumeConstraints);

    const float recoveredVolume = ComputeSignedTetrahedronVolume(
        solver.GetParticles()[p0].Position,
        solver.GetParticles()[p1].Position,
        solver.GetParticles()[p2].Position,
        solver.GetParticles()[p3].Position);
    assert(recoveredVolume > 0.0f);
    assert(NearlyEqual(recoveredVolume, volumeConstraints[0].RestVolume));
}

} // namespace Raven::ph::tests
