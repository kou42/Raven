#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Physics/Astro/Gravity/BarnesHutGravitySolver.h"
#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"
#include "Raven/Physics/Astro/OrbitalDiagnostics.h"

namespace Raven
{
class Scene;

namespace ph
{

enum class AstroGravitySolverMode
{
    Automatic,
    Direct,
    BarnesHut
};

// 重力定数などの物理法則とは独立した、RuntimeのSolver選択設定です。
// Automaticの既定値はRelease benchmarkの実測結果に基づき、1,000 bodyから
// theta=0.5のBarnes-Hutへ切り替え、800 body未満へ減るまで維持します。
struct AstroMultiRateSettings
{
    // 1なら従来どおり毎fixed-stepでGravity Solverを実行します。
    // N (> 1)ならsolve間のstepでは前回Forceを再利用し、Barnes-Hut近似誤差とは独立に
    // 更新頻度低下の影響を測定できるようにします。
    std::uint32_t GravityUpdateIntervalSteps = 1u;

    // 0以下ならNear/Far分離を無効化します。有効時はこの距離以内の相互作用を毎step再評価し、
    // それより遠い寄与だけをFarGravityUpdateIntervalStepsの周期で更新します。
    double NearGravityDistance = 0.0;
    std::uint32_t FarGravityUpdateIntervalSteps = 4u;

    // Debug/検証用です。有効時は現在snapshotをDirect Solverでも評価し、
    // 実際に適用するMulti-rate Forceとの差をPhysics LOD誤差として記録します。
    bool MeasureDirectReferenceError = false;
};

struct AstroGravitySolverSelectionSettings
{
    AstroGravitySolverMode Mode = AstroGravitySolverMode::Automatic;
    std::size_t BarnesHutBodyThreshold = 1000u;
    std::size_t DirectBodyThreshold = 800u;
    double BarnesHutTheta = 0.5;
};

// 天体力学DomainのScene境界です。
// 初期段階ではRigidBodyを積分器として再利用し、AstroWorldは状態収集・重力計算・Force feedbackだけを担当します。
class AstroWorld
{
public:
    void AccumulateGravityForces(Scene& scene, float fixedDeltaTime);
    void Clear();

    void SetGravitySolver(GravitySolver* solver);
    GravitySolver* GetGravitySolver() { return m_GravitySolver; }
    const GravitySolver* GetGravitySolver() const { return m_GravitySolver; }

    void SetGravitySolverSelectionSettings(
        const AstroGravitySolverSelectionSettings& settings);
    const AstroGravitySolverSelectionSettings& GetGravitySolverSelectionSettings() const;

    void SetGravitySolverSettings(const GravitySolverSettings& settings);
    const GravitySolverSettings& GetGravitySolverSettings() const { return m_Settings; }

    void SetMultiRateSettings(const AstroMultiRateSettings& settings);
    const AstroMultiRateSettings& GetMultiRateSettings() const { return m_MultiRateSettings; }
    const OrbitalDiagnostics& GetLastDiagnostics() const { return m_LastDiagnostics; }
    const AstroStatistics& GetStatistics() const { return m_Statistics; }

private:
    GravitySolver* ResolveGravitySolver(std::size_t bodyCount);
    bool CanReuseCachedGravityForces() const;
    bool IsNearFarMultiRateEnabled() const;
    void ComputeNearGravityForces(std::vector<AstroVector3>& outForces);
    void MeasureDirectReferenceError();
    void CacheGravityForces();
    void InvalidateGravityForceCache();

    DirectGravitySolver m_DirectGravitySolver{};
    BarnesHutGravitySolver m_BarnesHutGravitySolver{};
    // 外部Solverは非所有です。SetGravitySolverSelectionSettings()で内蔵選択へ戻ります。
    GravitySolver* m_CustomGravitySolver = nullptr;
    GravitySolver* m_GravitySolver = &m_DirectGravitySolver;
    AstroGravitySolverSelectionSettings m_SolverSelectionSettings{};
    bool m_AutomaticUsingBarnesHut = false;
    GravitySolverSettings m_Settings{};
    AstroMultiRateSettings m_MultiRateSettings{};
    std::uint32_t m_StepsSinceGravitySolve = 0u;
    std::vector<AstroBodyState> m_Bodies;
    std::vector<AstroVector3> m_Forces;
    std::vector<AstroBodyState> m_CachedGravityBodies;
    std::vector<AstroVector3> m_CachedGravityForces;
    std::vector<AstroVector3> m_CachedFarGravityForces;
    OrbitalDiagnostics m_LastDiagnostics{};
    AstroStatistics m_Statistics{};
};

} // namespace ph
} // namespace Raven
