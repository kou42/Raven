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

    // ParticleをCollider表面から僅かに離して保持する厚みです。
    // Clothを数学的な無厚み面として扱うと数値誤差でSphere内部へ戻りやすいため、
    // 最小限のCollision Thicknessを持たせます。
    float CollisionThickness = 0.005f;
};

// ============================================================================
// Static Sphere Collision Constraint
// ============================================================================
// SoftBody側で扱う最初のCollision Constraintです。
// 現段階ではSphere自体は動かず、Particleだけを球面外へ押し出します。
// RigidBodyとの双方向Impulse連成は、この基礎が安定してから別段階で追加します。
struct SoftBodySphereCollider
{
    math::Vec3 Center{};
    float Radius = 0.5f;
};

// ============================================================================
// Soft Body Solver
// ============================================================================
// RigidBodyのContactSolverとは分離した、Particle + Constraint用のXPBD Solverです。
// DistanceConstraintとCollision Constraintを同じPosition反復内で解くことで、
// ClothがCollider形状へ沿って変形する基本経路を構築します。
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

    // 静的Sphere Colliderを追加します。
    // 戻り値はCollider Indexで、将来Scene側から位置更新するときにも利用できます。
    uint32_t AddSphereCollider(const math::Vec3& center, float radius);
    void SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius);
    void ClearSphereColliders();

    void Clear();
    void Step(float deltaTime);

    std::vector<SoftBodyParticle>& GetParticles() { return m_Particles; }
    const std::vector<SoftBodyParticle>& GetParticles() const { return m_Particles; }
    const std::vector<XPBDDistanceConstraint>& GetDistanceConstraints() const { return m_DistanceConstraints; }
    const std::vector<SoftBodySphereCollider>& GetSphereColliders() const { return m_SphereColliders; }

private:
    void PredictPositions(float deltaTime);
    void ResetConstraintLambdas();
    void SolveDistanceConstraints(float deltaTime);
    void SolveSphereCollisions();
    void UpdateVelocities(float deltaTime);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
};

} // namespace ph
} // namespace Raven
