#pragma once

#include <vector>

#include "Raven/Physics/Astro/Gravity/DirectGravitySolver.h"

namespace Raven
{
class Scene;

namespace ph
{

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

    void SetGravitySolverSettings(const GravitySolverSettings& settings) { m_Settings = settings; }
    const GravitySolverSettings& GetGravitySolverSettings() const { return m_Settings; }

private:
    DirectGravitySolver m_DirectGravitySolver{};
    GravitySolver* m_GravitySolver = &m_DirectGravitySolver;
    GravitySolverSettings m_Settings{};
    std::vector<AstroBodyState> m_Bodies;
    std::vector<math::Vec3> m_Forces;
};

} // namespace ph
} // namespace Raven
