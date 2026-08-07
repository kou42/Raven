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

    const std::vector<ContactManifold>& GetContactManifolds() const
    {
        return m_Manifolds;
    }

    // Debug Drawや将来のPhysics QueryからTree状態を参照するため公開します。
    // Treeそのものの所有・更新責務はPhysicsWorld/BroadPhase側に残します。
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

    // Dynamic AABB Treeはフレームをまたいで保持することが重要です。
    // 毎Step作り直すとFat AABBによる「再挿入回避」の利点が失われます。
    BroadPhase m_BroadPhase;

    std::vector<ContactManifold> m_Manifolds;
};

} // namespace ph
} // namespace Raven
