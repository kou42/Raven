#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"

namespace Raven
{
namespace ph
{

struct SoftBodyCloth;
struct SoftBodySelfCollisionSettings;

// ============================================================================
// Cloth Particle-Triangle Self Collision Settings
// ============================================================================
struct SoftBodyParticleTriangleSelfCollisionSettings
{
    bool Enabled = true;

    // Particle中心とTriangle表面の間に維持する最小距離です。
    // Clothを数学的な厚み0の面として扱いつつ、自己貫通を防ぐための半厚みに相当します。
    float Thickness = 0.01f;

    // ========================================================================
    // Spatial Hash Cell Size Comparison Presets
    // ========================================================================
    // Narrow Phase最適化へ進む前に、Broad PhaseのCell Sizeを
    // 0.04 / 0.05 / 0.06で比較するための共通プリセットです。
    // Deformer・SelfTest・Editor/Profiler側が同じ値を参照できるようSettingsへ集約します。
    static constexpr float SpatialHashCellSizeSmall = 0.04f;
    static constexpr float SpatialHashCellSizeMedium = 0.05f;
    static constexpr float SpatialHashCellSizeLarge = 0.06f;

    // Particle-Triangle Broad Phase専用のSpatial Hash Cell Sizeです。
    // Thicknessとは独立した値です。
    // Cellを小さくすると1 Cellあたりの候補Triangleは減りますが、
    // Triangleが跨ぐCell数が増えてHashBuildが重くなります。
    // 大きくするとHashBuildは軽くなりますが、
    // Candidate/Narrow Phaseの候補数が増加します。
    //
    // 0.04 / 0.05 / 0.06の同一Solver snapshot比較では0.06が最短だったため、
    // 現在の通常デフォルトにはLargeを採用します。比較プリセット自体は継続して保持します。
    float SpatialHashCellSize = SpatialHashCellSizeLarge;

    // Particle-Triangle自己衝突専用の追加反復回数です。
    uint32_t SolverIterations = 4u;

    // XPBD Compliance。0.0fなら硬い自己衝突です。
    float Compliance = 0.0f;
};

// ============================================================================
// Particle-Triangle Spatial Hash Benchmark Result
// ============================================================================
// 0.04 / 0.05 / 0.06を「同じSolver状態」から比較した1サンプルです。
// Solverを値コピーしてから各Cell SizeでStepWithSelfCollisions()を1回ずつ実行するため、
// 実シミュレーションを順番に進める方式と異なり、Cloth変形状態の差が比較結果へ混ざりません。
struct SoftBodyParticleTriangleSpatialHashBenchmarkSample
{
    float CellSize = 0.0f;
    double SolverMilliseconds = 0.0;
    uint64_t CandidateCount = 0u;
    uint64_t NarrowPhaseCount = 0u;

    double GetNarrowPhaseRatio() const
    {
        if (CandidateCount == 0u)
        {
            return 0.0;
        }

        return static_cast<double>(NarrowPhaseCount)
            / static_cast<double>(CandidateCount);
    }
};

struct SoftBodyParticleTriangleSpatialHashBenchmarkResult
{
    static constexpr std::size_t SampleCount = 3u;

    std::array<SoftBodyParticleTriangleSpatialHashBenchmarkSample, SampleCount> Samples{};
    std::size_t BestSampleIndex = 0u;
    bool Valid = false;

    const SoftBodyParticleTriangleSpatialHashBenchmarkSample& GetBestSample() const
    {
        return Samples[BestSampleIndex];
    }
};

// ============================================================================
// Benchmark Particle-Triangle Spatial Hash Cell Sizes
// ============================================================================
// 現在のSolverを3つコピーし、0.04 / 0.05 / 0.06を完全に同じ初期状態から1Stepずつ実行します。
// 最適値はParticle-Triangleだけの候補数ではなく、Cell Size変更によるHash Build / Candidate /
// Narrow Phaseの影響を含む統合Solver実行時間が最小のSampleとして選択します。
//
// この関数は入力solverを変更しません。比較はデバッグ・調整用であり、通常StepのHot Pathには入りません。
inline SoftBodyParticleTriangleSpatialHashBenchmarkResult BenchmarkSoftBodyParticleTriangleSpatialHashCellSizes(
    const SoftBodySolver& solver,
    const SoftBodyCloth& cloth,
    float deltaTime,
    const SoftBodySelfCollisionSettings& particleSettings,
    const SoftBodyParticleTriangleSelfCollisionSettings& particleTriangleSettings)
{
    SoftBodyParticleTriangleSpatialHashBenchmarkResult result{};

    if (deltaTime <= 0.0f || particleTriangleSettings.Enabled == false)
    {
        return result;
    }

    const std::array<float, SoftBodyParticleTriangleSpatialHashBenchmarkResult::SampleCount> cellSizes{
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeSmall,
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeMedium,
        SoftBodyParticleTriangleSelfCollisionSettings::SpatialHashCellSizeLarge
    };

    using Clock = std::chrono::steady_clock;

    for (std::size_t sampleIndex = 0u; sampleIndex < cellSizes.size(); ++sampleIndex)
    {
        // 重要:
        // 3候補を同じsolverから毎回コピーすることで、前候補のPosition補正やVelocity更新を
        // 次候補へ持ち越しません。Cell Size以外の入力条件を完全に揃えて比較します。
        SoftBodySolver solverCopy = solver;
        SoftBodyParticleTriangleSelfCollisionSettings settingsCopy = particleTriangleSettings;
        settingsCopy.SpatialHashCellSize = cellSizes[sampleIndex];

        const Clock::time_point start = Clock::now();
        solverCopy.StepWithSelfCollisions(
            deltaTime,
            cloth,
            particleSettings,
            settingsCopy);
        const Clock::time_point end = Clock::now();

        const SoftBodyParticleTriangleCollisionStatistics& statistics =
            solverCopy.GetParticleTriangleCollisionStatistics();

        SoftBodyParticleTriangleSpatialHashBenchmarkSample& sample = result.Samples[sampleIndex];
        sample.CellSize = cellSizes[sampleIndex];
        sample.SolverMilliseconds =
            std::chrono::duration<double, std::milli>(end - start).count();
        sample.CandidateCount = statistics.CandidateCount;
        sample.NarrowPhaseCount = statistics.NarrowPhaseCount;

        // 最初のSampleを基準にし、その後は統合Solver時間が短いものだけで更新します。
        // 候補数だけで決めないのは、小CellによるHashBuild増加も含めて評価するためです。
        if (sampleIndex == 0u
            || sample.SolverMilliseconds < result.Samples[result.BestSampleIndex].SolverMilliseconds)
        {
            result.BestSampleIndex = sampleIndex;
        }
    }

    result.Valid = true;
    return result;
}

// ============================================================================
// Solve Cloth Particle-Triangle Self Collisions
// ============================================================================
// Cloth grid topologyからTriangleを復元し、Spatial Hash Broad Phaseで候補を絞った後、
// ParticleとTriangle最近傍点の距離ConstraintをXPBDで解きます。
//
// 自分自身を頂点として含むTriangleは必ず除外します。
// さらにTriangle補正は最近傍点のBarycentric Weightで3頂点へ分配するため、
// Particleだけを一方的に押すのではなくCloth同士の接触として双方へ反作用が伝わります。
void SolveSoftBodyParticleTriangleSelfCollisions(
    SoftBodySolver& solver,
    const SoftBodyCloth& cloth,
    float deltaTime,
    const SoftBodyParticleTriangleSelfCollisionSettings& settings);

} // namespace ph
} // namespace Raven
