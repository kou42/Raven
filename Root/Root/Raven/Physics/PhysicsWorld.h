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
    // RayCastで最終的に採用された最短ヒット情報です。
    // Fractionは origin + direction * Fraction で交点を再構築できます。
    Entity HitEntity{};
    math::Vec3 Point{};
    math::Vec3 Normal{};
    float Fraction = 0.0f;
};

struct PhysicsRayCastFilter
{
    bool IncludeStatic = false;
    bool IncludeKinematic = false;
    bool IncludeDynamic = true;
    bool IncludePlanes = false;

    static PhysicsRayCastFilter All()
    {
        PhysicsRayCastFilter filter{};
        filter.IncludeStatic = true;
        filter.IncludeKinematic = true;
        filter.IncludeDynamic = true;
        filter.IncludePlanes = true;
        return filter;
    }
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

    void Reset() { *this = PhysicsSolverDebugStatistics{}; }
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
    const PhysicsSolverDebugStatistics& GetSolverDebugStatistics() const { return m_SolverDebugStatistics; }

    // ========================================================================
    // Collision Ignore Pair API
    // ========================================================================
    // Jointで接続されたBodyなど、特定のEntity同士だけ衝突させないためのAPIです。
    // Layer/Maskのようなカテゴリ単位ではなく、インスタンス単位の除外に使用します。
    //
    // BroadPhase内部ではEntityのGenerationを含むHandle値で管理するため、
    // Entity Indexが再利用されても古い除外設定が新Entityへ漏れません。
    void AddIgnoreCollisionPair(Entity a, Entity b)
    {
        m_BroadPhase.AddIgnorePair(a, b);
    }

    void RemoveIgnoreCollisionPair(Entity a, Entity b)
    {
        m_BroadPhase.RemoveIgnorePair(a, b);
    }

    void RemoveIgnoreCollisionPairsForEntity(Entity entity)
    {
        m_BroadPhase.RemoveIgnorePairsForEntity(entity);
    }

    bool IsCollisionPairIgnored(Entity a, Entity b) const
    {
        return m_BroadPhase.IsPairIgnored(a, b);
    }

    void ClearIgnoreCollisionPairs()
    {
        m_BroadPhase.ClearIgnorePairs();
    }

    // ========================================================================
    // 剛体制御用API
    // ========================================================================
    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);
    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);
    void AddTorque(Scene& scene, Entity entity, const math::Vec3& torque);

    void AddImpulseAtPoint(Scene& scene, Entity entity, const math::Vec3& impulse,
        const math::Vec3& worldPoint);

    void SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity);
    math::Vec3 GetLinearVelocity(const Scene& scene, Entity entity) const;
    void Teleport(Scene& scene, Entity entity, const math::Vec3& position);
    void MovePosition(Scene& scene, Entity entity, const math::Vec3& position);
    void WakeUp(Scene& scene, Entity entity);

    bool RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
        float maxFraction, PhysicsRayCastHit& outHit);

    bool RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
        float maxFraction, const PhysicsRayCastFilter& filter, PhysicsRayCastHit& outHit);

    void QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities);

    const std::vector<ContactManifold>& GetContactManifolds() const { return m_Manifolds; }
    const BroadPhase& GetBroadPhase() const { return m_BroadPhase; }
    const std::vector<BroadPhasePair>& GetBroadPhasePairs() const { return m_BroadPhase.GetLastPairs(); }

private:
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);
    void DetectCollisions(Scene& scene);
    void RestorePersistentContacts(Scene& scene);
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
