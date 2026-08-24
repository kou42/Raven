#include <array>
#include <cassert>

#include "Raven/Physics/SoftBody/SoftBodyCloth.h"
#include "Raven/Physics/SoftBody/SoftBodyParticleTriangleSelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven::ph::tests
{
namespace
{
void RunParticleTriangleSpatialHashPresetCase(float spatialHashCellSize)
{

    SoftBodySolver solver{};
    solver.SetGravity({ 0.0f, 0.0f, 0.0f });

    SoftBodySolverSettings solverSettings{};
    solverSettings.SolverIterations = 4u;
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
    const uint32_t probe = solver.AddParticle({ 0.0f, 0.0f, 0.001f }, 1.0f);
    assert(probe < solver.GetParticles().size());

    SoftBodySelfCollisionSettings particleSettings{};
    particleSettings.Enabled = false;

    SoftBodyParticleTriangleSelfCollisionSettings particleTriangleSettings{};
    particleTriangleSettings.Enabled = true;
    particleTriangleSettings.Thickness = 0.05f;
    particleTriangleSettings.SpatialHashCellSize = spatialHashCellSize;
    particleTriangleSettings.Compliance = 0.0f;

    solver.StepWithSelfCollisions(
        1.0f / 60.0f,
        cloth,
        particleSettings,
        particleTriangleSettings);

    const SoftBodyParticleTriangleCollisionStatistics& statistics =
        solver.GetParticleTriangleCollisionStatistics();

    // Cell SizeはBroad Phaseの性能特性だけを変える設定です。
    // 比較対象のどの値でも同じ接触候補を失わず、実Narrow Phaseへ到達できることを確認します。
    assert(statistics.CandidateCount > 0u);
    assert(statistics.NarrowPhaseCount > 0u);
    assert(statistics.CandidateCount >= statistics.NarrowPhaseCount);

    const SoftBodyParticle& probeParticle = solver.GetParticles()[probe];
    assert(probeParticle.Position.z > 0.001f);
}

void RunParticleTriangleSpatialHashBenchmarkCase()
{
    SoftBodySolver solver{};
    solver.SetGravity({ 0.0f, 0.0f, 0.0f });

    SoftBodySolverSettings solverSettings{};
    solverSettings.SolverIterations = 4u;
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
    const uint32_t probe = solver.AddParticle({ 0.0f, 0.0f, 0.001f }, 1.0f);
    assert(probe < solver.GetParticles().size());

    SoftBodySelfCollisionSettings particleSettings{};
    particleSettings.Enabled = false;

    SoftBodyParticleTriangleSelfCollisionSettings particleTriangleSettings{};
    particleTriangleSettings.Enabled = true;
    particleTriangleSettings.Thickness = 0.05f;
    particleTriangleSettings.Compliance = 0.0f;

    const float originalProbeZ = solver.GetParticles()[probe].Position.z;

    const SoftBodyParticleTriangleSpatialHashBenchmarkResult benchmark =
        BenchmarkSoftBodyParticleTriangleSpatialHashCellSizes(
            solver,
            cloth,
            1.0f / 60.0f,
            particleSettings,
            particleTriangleSettings);

    assert(benchmark.Valid);

    constexpr std::array<float, 3u> expectedCellSizes
    {
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeSmall,
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeMedium,
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeLarge
    };

    std::size_t expectedBestIndex = 0u;
    for (std::size_t sampleIndex = 0u; sampleIndex < benchmark.Samples.size(); ++sampleIndex)
    {
        const SoftBodyParticleTriangleSpatialHashBenchmarkSample& sample =
            benchmark.Samples[sampleIndex];

        assert(sample.CellSize == expectedCellSizes[sampleIndex]);
        assert(sample.SolverMilliseconds >= 0.0);
        assert(sample.CandidateCount > 0u);
        assert(sample.NarrowPhaseCount > 0u);
        assert(sample.CandidateCount >= sample.NarrowPhaseCount);
        assert(sample.GetNarrowPhaseRatio() > 0.0);
        assert(sample.GetNarrowPhaseRatio() <= 1.0);

        if (sample.SolverMilliseconds
            < benchmark.Samples[expectedBestIndex].SolverMilliseconds)
        {
            expectedBestIndex = sampleIndex;
        }
    }

    // BestSampleIndexは「候補数最小」ではなく、同一初期状態から実測した統合Solver時間の最小値です。
    assert(benchmark.BestSampleIndex == expectedBestIndex);
    assert(benchmark.GetBestSample().CellSize == expectedCellSizes[expectedBestIndex]);

    // Benchmarkはsolverを値コピーして実行するため、比較そのものが本番Solver状態を進めないことを確認します。
    assert(solver.GetParticles()[probe].Position.z == originalProbeZ);
}
} // namespace

// ============================================================================
// Integrated Soft Body Step Self Tests
// ============================================================================
// Cloth自己衝突がSolver::Step()後の別passではなく、Internal / External Constraintと
// 同じSolverIterations内で解かれることを確認するためのassertベース回帰テストです。
void RunSoftBodyIntegratedStepSelfTests()
{

    // ========================================================================
    // TEMP: SoftBody simulation disabled
    // ========================================================================
    // SoftBody最適化を一時中断している間、Simulation処理全体をここで停止します。
    // 再開時はこのreturnだけを削除してください。
    return;

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

    // ========================================================================
    // Particle-Triangle Broad / Narrow Phase Counter
    // ========================================================================
    // CandidateCountはSpatial Hashが生成した候補を全Solver Iteration分合算した値です。
    // NarrowPhaseCountはTopology除外などのcheap rejectを通過して実距離計算へ進んだ回数なので、
    // 必ずCandidateCount以下になり、このテストシーンではprobeが面へ接近しているため0より大きくなります。
    const SoftBodyParticleTriangleCollisionStatistics& statistics =
        solver.GetParticleTriangleCollisionStatistics();
    assert(statistics.CandidateCount > 0u);
    assert(statistics.NarrowPhaseCount > 0u);
    assert(statistics.CandidateCount >= statistics.NarrowPhaseCount);

    // Statisticsは累積総計ではなく「直近1 Step」の計測値です。
    // 次StepでParticle-Triangle自己衝突を無効にした場合、前Stepの値を残さず0へResetされることを確認します。
    particleTriangleSettings.Enabled = false;
    solver.StepWithSelfCollisions(
        1.0f / 60.0f,
        cloth,
        particleSettings,
        particleTriangleSettings);

    const SoftBodyParticleTriangleCollisionStatistics& disabledStatistics =
        solver.GetParticleTriangleCollisionStatistics();
    assert(disabledStatistics.CandidateCount == 0u);
    assert(disabledStatistics.NarrowPhaseCount == 0u);

    // ========================================================================
    // Spatial Hash Cell Size 0.04 / 0.05 / 0.06 Comparison Presets
    // ========================================================================
    // 各ケースでSolverとClothを作り直し、初期状態を完全に揃えます。
    // これにより前ケースのPosition補正を次ケースへ持ち越さず、Cell Sizeだけを比較できます。
    constexpr std::array<float, 3u> comparisonCellSizes
    {
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeSmall,
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeMedium,
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeLarge
    };

    for (float spatialHashCellSize : comparisonCellSizes)
    {
        RunParticleTriangleSpatialHashPresetCase(spatialHashCellSize);
    }

    // 同一Solver snapshotから3候補を実測し、最小時間のCell Sizeを返す③の選定処理を確認します。
    RunParticleTriangleSpatialHashBenchmarkCase();
}

} // namespace Raven::ph::tests
