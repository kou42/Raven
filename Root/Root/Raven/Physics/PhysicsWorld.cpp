#include <algorithm>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Solver/ContactSolver.h"

namespace Raven
{
namespace ph
{
namespace
{
void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}
}

void PhysicsWorld::SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
const math::Vec3& PhysicsWorld::GetGravity() const { return m_Gravity; }

void PhysicsWorld::ApplyForces(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity); static_cast<void>(transform);
        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping || rigidBody.InverseMass <= 0.0f) continue;

        math::Vec3 acceleration{};
        if (rigidBody.UseGravity) acceleration += m_Gravity;
        acceleration += rigidBody.Force * rigidBody.InverseMass;
        rigidBody.LinearVelocity += acceleration * dt;
    }
}

void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping) continue;
        const float linearDamping = std::max(rigidBody.LinearDamping, 0.0f);
        rigidBody.LinearVelocity *= 1.0f / (1.0f + linearDamping * dt);
    }
}

void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type == BodyType::Static || (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping)) continue;
        transform.Position += rigidBody.LinearVelocity * dt;
    }
}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    m_Manifolds.clear();

    // ========================================================================
    // Dynamic AABB Tree Broad Phase
    // ========================================================================
    // m_BroadPhaseはPhysicsWorldのメンバとしてフレームをまたいで保持します。
    // ここが重要で、ローカル変数として毎Step作り直すとFat AABB内に収まる移動を
    // 再挿入せずに済ませるDynamic Treeの利点が完全に失われます。
    std::vector<BroadPhasePair> candidatePairs;
    m_BroadPhase.ComputePairs(scene, candidatePairs);

    // finite collider narrow phase
    for (const BroadPhasePair& pair : candidatePairs)
    {
        if (!scene.IsEntityAlive(pair.A) || !scene.IsEntityAlive(pair.B)) continue;

        TransformComponent* transformA = scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());
        TransformComponent* transformB = scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
        ColliderComponent* colliderA = scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());
        ColliderComponent* colliderB = scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());
        if (!transformA || !transformB || !colliderA || !colliderB) continue;

        if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Sphere)
        {
            ContactManifold manifold{};
            if (GenerateSphereSphereManifold(pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold))
            {
                m_Manifolds.push_back(manifold);
            }
        }
    }

    // Planeは無限形状なのでDynamic AABB Treeへは登録しません。
    for (auto [sphereEntity, sphereTransform, sphereCollider] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (sphereCollider.Type != ColliderType::Sphere) continue;

        for (auto [planeEntity, planeTransform, planeCollider] : scene.View<TransformComponent, ColliderComponent>())
        {
            if (planeCollider.Type != ColliderType::Plane || sphereEntity == planeEntity) continue;

            ContactManifold manifold{};
            if (GenerateSpherePlaneManifold(sphereEntity, sphereTransform, sphereCollider, planeEntity, planeTransform, planeCollider, manifold))
            {
                m_Manifolds.push_back(manifold);
            }
        }
    }
}

void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    for (ContactManifold& manifold : m_Manifolds) SolveContactManifold(scene, manifold, dt);
}

void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type != BodyType::Dynamic) continue;
        if (!rigidBody.AllowSleep) { WakeRigidBody(rigidBody); continue; }
        if (rigidBody.IsSleeping) { rigidBody.LinearVelocity = math::Vec3{}; continue; }

        const float threshold = std::max(rigidBody.SleepThreshold, 0.0f);
        if (rigidBody.LinearVelocity.LengthSq() <= threshold * threshold)
        {
            rigidBody.SleepTimer += dt;
            const float requiredSleepTime = std::max(rigidBody.SleepTimeThreshold, 0.0f);
            if (rigidBody.SleepTimer >= requiredSleepTime)
            {
                rigidBody.IsSleeping = true;
                rigidBody.LinearVelocity = math::Vec3{};
                rigidBody.SleepTimer = requiredSleepTime;
            }
        }
        else rigidBody.SleepTimer = 0.0f;
    }
}

void PhysicsWorld::ClearForces(Scene& scene)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        rigidBody.Force = math::Vec3{};
        rigidBody.Torque = math::Vec3{};
    }
}

void PhysicsWorld::AddForce(Scene& scene, Entity entity, const math::Vec3& force)
{
    if (!scene.IsEntityAlive(entity)) return;
    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f) return;
    WakeRigidBody(*rigidBody);
    rigidBody->Force += force;
}

void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (!scene.IsEntityAlive(entity)) return;
    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f) return;
    WakeRigidBody(*rigidBody);
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
}

void PhysicsWorld::WakeUp(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity)) return;
    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody && rigidBody->Type == BodyType::Dynamic) WakeRigidBody(*rigidBody);
}

void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f) return;
    ApplyForces(scene, dt);
    IntegrateVelocities(scene, dt);
    IntegratePositions(scene, dt);
    DetectCollisions(scene);
    SolveCollisions(scene, dt);
    UpdateSleeping(scene, dt);
    ClearForces(scene);
}

} // namespace ph
} // namespace Raven
