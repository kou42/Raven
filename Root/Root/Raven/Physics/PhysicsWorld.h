#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Core/Memory/FrameAllocator.h"
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

struct PhysicsGroundQueryHit
{
    Entity HitEntity{};
    math::Vec3 Point{};
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Distance = 0.0f;
};

struct PhysicsGroundQuerySettings
{
    float MaxDistance = 1.0f;
    float MaxSlopeRadians = 0.872664626f;
    bool IncludeStatic = true;
    bool IncludeKinematic = true;
    bool IncludeDynamic = false;
    bool IncludePlanes = true;
};

struct PhysicsCapsuleCastHit
{
    Entity HitEntity{};
    math::Vec3 Position{};
    math::Vec3 Point{};
    math::Vec3 Normal{};
    float Fraction = 0.0f;
};

struct PhysicsCapsuleCastSettings
{
    float Radius = 0.35f;
    float HalfLength = 0.55f;
    float SkinWidth = 0.02f;
    uint32_t MaxSubsteps = 64u;
    uint32_t BinarySearchIterations = 10u;
    bool IncludeStatic = true;
    bool IncludeKinematic = true;
    bool IncludeDynamic = false;
    bool IncludePlanes = true;
    bool IncludeTriggers = false;
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

// Physics専用FrameAllocatorの観測値です。
// 現段階ではBroadPhase候補列を最初の移行対象とし、容量は実測値を見て調整します。
struct PhysicsFrameAllocatorStatistics
{
    std::size_t Capacity = 0;
    std::size_t LastFrameUsedMemory = 0;
    std::size_t PeakUsedMemory = 0;
    std::size_t LastFrameAllocationCount = 0;
};

class PhysicsWorld
{
public:
    // FrameAllocatorはPhysicsWorldより短命な一時データ専用です。
    // constructorをheader側で定義しておくことで、既存PhysicsWorld.cppを変更せず
    // allocator導入の土台だけを安全に追加できます。
    PhysicsWorld()
        : m_FrameAllocator(PhysicsFrameAllocatorCapacity)
    {
        m_FrameAllocatorStatistics.Capacity = m_FrameAllocator.GetCapacity();
    }

    void SetGravity(const math::Vec3& gravity);
    const math::Vec3& GetGravity() const;
    void Step(Scene& scene, float fixedDeltaTime);

    void SetSolverSettings(const ContactSolverSettings& settings) { m_SolverSettings = settings; }
    ContactSolverSettings& GetSolverSettings() { return m_SolverSettings; }
    const ContactSolverSettings& GetSolverSettings() const { return m_SolverSettings; }
    const PhysicsSolverDebugStatistics& GetSolverDebugStatistics() const { return m_SolverDebugStatistics; }
    const PhysicsFrameAllocatorStatistics& GetFrameAllocatorStatistics() const { return m_FrameAllocatorStatistics; }

    void AddIgnoreCollisionPair(Entity a, Entity b) { m_BroadPhase.AddIgnorePair(a, b); }
    void RemoveIgnoreCollisionPair(Entity a, Entity b) { m_BroadPhase.RemoveIgnorePair(a, b); }
    void RemoveIgnoreCollisionPairsForEntity(Entity entity) { m_BroadPhase.RemoveIgnorePairsForEntity(entity); }
    bool IsCollisionPairIgnored(Entity a, Entity b) const { return m_BroadPhase.IsPairIgnored(a, b); }
    void ClearIgnoreCollisionPairs() { m_BroadPhase.ClearIgnorePairs(); }

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
    bool GroundQuery(Scene& scene, const math::Vec3& origin,
        const PhysicsGroundQuerySettings& settings, PhysicsGroundQueryHit& outHit);
    bool CapsuleCast(Scene& scene, const math::Vec3& startFootPosition,
        const math::Vec3& displacement, const PhysicsCapsuleCastSettings& settings,
        PhysicsCapsuleCastHit& outHit);
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
    static constexpr std::size_t PhysicsFrameAllocatorCapacity = 256u * 1024u;

    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    BroadPhase m_BroadPhase;
    ContactSolverSettings m_SolverSettings{};
    PhysicsSolverDebugStatistics m_SolverDebugStatistics{};
    FrameAllocator m_FrameAllocator;
    PhysicsFrameAllocatorStatistics m_FrameAllocatorStatistics{};
    std::vector<ContactManifold> m_Manifolds;
    std::vector<ContactManifold> m_PreviousManifolds;
};

} // namespace ph
} // namespace Raven