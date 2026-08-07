#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Contact.h"
#include "Raven/Physics/Collision/BroadPhase.h"
#include "Raven/Physics/Solver/ContactSolver.h"

namespace Raven
{
class Entity;
class Scene;

namespace ph
{

struct PhysicsRayCastHit
{
    Entity HitEntity{};
    math::Vec3 Point{};
    math::Vec3 Normal{};
    float Fraction = 0.0f;
};

struct PhysicsSolverDebugStatistics
{
    uint32_t ManifoldCount = 0;
    uint32_t ContactPointCount = 0;
    uint32_t PersistentManifoldCount = 0;
    uint32_t PersistentContactPointCount = 0;
    uint32_t WarmStartedConstraintCount = 0;
    uint32_t VelocityIterations = 0;
    float MaxPenetration = 0.0f;
    float MaxNormalImpulse = 0.0f;
    float MaxFrictionImpulse = 0.0f;

    void Reset()
    {
        *this = PhysicsSolverDebugStatistics{};
    }
};

class PhysicsWorld
{
public:
    void SetGravity(const math::Vec3& gravity);
    const math::Vec3& GetGravity() const;
    void Step(Scene& scene, float fixedDeltaTime);

    void SetSolverSettings(const ContactSolverSettings& settings) { m_SolverSettings = settings; }
    ContactSolverSettings& GetSolverSettings() { return m_SolverSettings; }
    const ContactSolverSettings& GetSolverSettings() const { return m_SolverSettings; }

    // 直近Physics StepのSolver診断値です。
    const PhysicsSolverDebugStatistics& GetSolverDebugStatistics() const
    {
        return m_SolverDebugStatistics;
    }

    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);
    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);
    void AddTorque(Scene& scene, Entity entity, const math::Vec3& torque);
    void SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity);
    math::Vec3 GetLinearVelocity(const Scene& scene, Entity entity) const;
    void Teleport(Scene& scene, Entity entity, const math::Vec3& position);
    void MovePosition(Scene& scene, Entity entity, const math::Vec3& position);
    void WakeUp(Scene& scene, Entity entity);

    bool RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
        float maxFraction, PhysicsRayCastHit& outHit);
    void QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities);

    // Debug Overlayは以下の読み取りAPIだけを使います。
    // Simulationデータを再計算せず、同一Stepの状態を観測することが重要です。
    const std::vector<ContactManifold>& GetContactManifolds() const { return m_Manifolds; }
    const BroadPhase& GetBroadPhase() const { return m_BroadPhase; }
    const std::vector<BroadPhasePair>& GetBroadPhasePairs() const
    {
        return m_BroadPhase.GetLastPairs();
    }

private:
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);
    void DetectCollisions(Scene& scene);
    void RestorePersistentContacts();
    void SolveCollisions(Scene& scene, float dt);
    void UpdateSolverDebugStatisticsAfterSolve();
    void UpdateSleeping(Scene& scene, float dt);
    void ClearForces(Scene& scene);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    BroadPhase m_BroadPhase;
    ContactSolverSettings m_SolverSettings{};
    PhysicsSolverDebugStatistics m_SolverDebugStatistics{};
    std::vector<ContactManifold> m_Manifolds;
    std::vector<ContactManifold> m_PreviousManifolds;
};

} // namespace ph
} // namespace Raven
