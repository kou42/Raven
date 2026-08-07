#pragma once

#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Contact.h"
#include "Raven/Physics/Collision/BroadPhase.h"

namespace Raven
{

class Entity;
class Scene;

namespace ph
{

// RayCastで最も近いColliderに当たった結果です。
// Fractionは Point = Origin + Direction * Fraction の係数なので、
// Directionを正規化した場合はそのまま距離として扱えます。
struct PhysicsRayCastHit
{
    Entity HitEntity{};
    math::Vec3 Point{};
    math::Vec3 Normal{};
    float Fraction = 0.0f;
};

class PhysicsWorld
{
public:
    void SetGravity(const math::Vec3& gravity);
    const math::Vec3& GetGravity() const;

    void Step(Scene& scene, float fixedDeltaTime);

    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);
    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);
    void AddTorque(Scene& scene, Entity entity, const math::Vec3& torque);

    void SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity);
    math::Vec3 GetLinearVelocity(const Scene& scene, Entity entity) const;

    void Teleport(Scene& scene, Entity entity, const math::Vec3& position);
    void MovePosition(Scene& scene, Entity entity, const math::Vec3& position);
    void WakeUp(Scene& scene, Entity entity);

    // ------------------------------------------------------------------------
    // Physics Query API
    // ------------------------------------------------------------------------
    // Dynamic AABB Treeで候補を高速に絞り込み、その後tight AABB/実Colliderで
    // false positiveを除去します。
    //
    // RayCastは最も近い1件を返します。directionは正規化不要です。
    // maxFractionは origin + direction * maxFraction までを探索範囲とします。
    bool RayCast(
        Scene& scene,
        const math::Vec3& origin,
        const math::Vec3& direction,
        float maxFraction,
        PhysicsRayCastHit& outHit);

    // queryBoundsと実際のtight AABBが重なる有限Colliderを列挙します。
    // Planeは無限形状のためAABB Query対象外です。
    void QueryAABB(
        Scene& scene,
        const AABB& queryBounds,
        std::vector<Entity>& outEntities);

    const std::vector<ContactManifold>& GetContactManifolds() const
    {
        return m_Manifolds;
    }

    const BroadPhase& GetBroadPhase() const
    {
        return m_BroadPhase;
    }

private:
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);

    void DetectCollisions(Scene& scene);
    void SolveCollisions(Scene& scene, float dt);

    void UpdateSleeping(Scene& scene, float dt);
    void ClearForces(Scene& scene);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    BroadPhase m_BroadPhase;
    std::vector<ContactManifold> m_Manifolds;
};

} // namespace ph
} // namespace Raven
