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
    uint32_t SolverIterations = 8u;

    // ParticleをCollider表面から僅かに離して保持する共通厚みです。
    float CollisionThickness = 0.005f;
};

struct SoftBodySphereCollider
{
    math::Vec3 Center{};
    float Radius = 0.5f;
};

// ============================================================================
// Static Plane Collision Constraint
// ============================================================================
// Plane式は dot(Normal, x) - Offset = 0 とします。
// Normal側を外側とみなし、signed distanceがCollisionThickness未満のParticleを外側へ押し戻します。
struct SoftBodyPlaneCollider
{
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Offset = 0.0f;
};

class SoftBodySolver
{
public:
    void SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
    const math::Vec3& GetGravity() const { return m_Gravity; }

    void SetSettings(const SoftBodySolverSettings& settings) { m_Settings = settings; }
    SoftBodySolverSettings& GetSettings() { return m_Settings; }
    const SoftBodySolverSettings& GetSettings() const { return m_Settings; }

    uint32_t AddParticle(const math::Vec3& position, float inverseMass = 1.0f);
    uint32_t AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance = 0.0f);

    uint32_t AddSphereCollider(const math::Vec3& center, float radius);
    void SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius);
    void ClearSphereColliders();

    uint32_t AddPlaneCollider(const math::Vec3& normal, float offset);
    void SetPlaneCollider(uint32_t colliderIndex, const math::Vec3& normal, float offset);
    void ClearPlaneColliders();

    void Clear();
    void Step(float deltaTime);

    std::vector<SoftBodyParticle>& GetParticles() { return m_Particles; }
    const std::vector<SoftBodyParticle>& GetParticles() const { return m_Particles; }
    const std::vector<XPBDDistanceConstraint>& GetDistanceConstraints() const { return m_DistanceConstraints; }
    const std::vector<SoftBodySphereCollider>& GetSphereColliders() const { return m_SphereColliders; }
    const std::vector<SoftBodyPlaneCollider>& GetPlaneColliders() const { return m_PlaneColliders; }

private:
    void PredictPositions(float deltaTime);
    void ResetConstraintLambdas();
    void SolveDistanceConstraints(float deltaTime);
    void SolveSphereCollisions();
    void SolvePlaneCollisions();
    void UpdateVelocities(float deltaTime);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
    std::vector<SoftBodyPlaneCollider> m_PlaneColliders;
};

} // namespace ph
} // namespace Raven
