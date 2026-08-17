#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
#include "Raven/Physics/SoftBody/XPBDDistanceConstraint.h"

namespace Raven
{
namespace ph
{

struct SoftBodySolverSettings
{
    // Position constraintを繰り返し解く回数です。
    // XPBDでは反復回数を増やすほど制約誤差が小さくなります。
    uint32_t SolverIterations = 8u;
};

// ============================================================================
// Soft Body Solver
// ============================================================================
// RigidBodyのContactSolverとは分離した、Particle + Constraint用のXPBD Solverです。
// 最初の段階ではDistanceConstraintだけを扱い、Clothの基本となる伸び制約を実装します。
// 将来はBending / Volume / Collision constraintを同じStepへ追加できます。
class SoftBodySolver
{
public:
    void SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
    const math::Vec3& GetGravity() const { return m_Gravity; }

    void SetSettings(const SoftBodySolverSettings& settings) { m_Settings = settings; }
    SoftBodySolverSettings& GetSettings() { return m_Settings; }
    const SoftBodySolverSettings& GetSettings() const { return m_Settings; }

    // Particleを追加し、そのIndexを返します。
    uint32_t AddParticle(const math::Vec3& position, float inverseMass = 1.0f);

    // 現在のParticle間距離をRestLengthとしてDistanceConstraintを追加します。
    uint32_t AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance = 0.0f);

    void Clear();
    void Step(float deltaTime);

    std::vector<SoftBodyParticle>& GetParticles() { return m_Particles; }
    const std::vector<SoftBodyParticle>& GetParticles() const { return m_Particles; }
    const std::vector<XPBDDistanceConstraint>& GetDistanceConstraints() const { return m_DistanceConstraints; }

private:
    void PredictPositions(float deltaTime);
    void ResetConstraintLambdas();
    void SolveDistanceConstraints(float deltaTime);
    void UpdateVelocities(float deltaTime);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
};

} // namespace ph
} // namespace Raven
