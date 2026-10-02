#pragma once

#include <cstddef>
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

    void SetGravitySolverSettings(const GravitySolverSettings& settings) { m_Settings = settings; }
    const GravitySolverSettings& GetGravitySolverSettings() const { return m_Settings; }
    const OrbitalDiagnostics& GetLastDiagnostics() const { return m_LastDiagnostics; }
    const AstroStatistics& GetStatistics() const { return m_Statistics; }

private:
    GravitySolver* ResolveGravitySolver(std::size_t bodyCount);

    DirectGravitySolver m_DirectGravitySolver{};
    BarnesHutGravitySolver m_BarnesHutGravitySolver{};
    // 外部Solverは非所有です。SetGravitySolverSelectionSettings()で内蔵選択へ戻ります。
    GravitySolver* m_CustomGravitySolver = nullptr;
    GravitySolver* m_GravitySolver = &m_DirectGravitySolver;
    AstroGravitySolverSelectionSettings m_SolverSelectionSettings{};
    bool m_AutomaticUsingBarnesHut = false;
    GravitySolverSettings m_Settings{};
    std::vector<AstroBodyState> m_Bodies;
    std::vector<AstroVector3> m_Forces;
    OrbitalDiagnostics m_LastDiagnostics{};
    AstroStatistics m_Statistics{};
};

} // namespace ph
} // namespace Raven
