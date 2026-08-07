#include <algorithm>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/Collision/BroadPhase.h"
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

void PhysicsWorld::SetGravity(const math::Vec3& gravity)
{
    m_Gravity = gravity;
}

const math::Vec3& PhysicsWorld::GetGravity() const
{
    return m_Gravity;
}

void PhysicsWorld::ApplyForces(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody]
        : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);
        static_cast<void>(transform);

        if (rigidBody.Type != BodyType::Dynamic
            || rigidBody.IsSleeping
            || rigidBody.InverseMass <= 0.0f)
        {
            continue;
        }

        math::Vec3 acceleration{};
        if (rigidBody.UseGravity)
        {
            acceleration += m_Gravity;
        }
        acceleration += rigidBody.Force * rigidBody.InverseMass;
        rigidBody.LinearVelocity += acceleration * dt;
    }
}

void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping)
        {
            continue;
        }

        const float linearDamping = std::max(rigidBody.LinearDamping, 0.0f);
        rigidBody.LinearVelocity *= 1.0f / (1.0f + linearDamping * dt);
    }
}

void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody]
        : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type == BodyType::Static
            || (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping))
        {
            continue;
        }

        transform.Position += rigidBody.LinearVelocity * dt;
    }
}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    m_Manifolds.clear();

    // ========================================================================
    // Broad Phase
    // ========================================================================
    // Sphere / Boxの有限ColliderをAABBへ変換し、X軸Sweep-and-Pruneで
    // 「実際に衝突している可能性がある組」だけを抽出します。
    //
    // 重要:
    // Broad PhaseのAABB重なりは衝突確定ではありません。
    // ここではfalse positiveを許し、候補をNarrow Phaseへ渡すことが役目です。
    BroadPhase broadPhase;
    std::vector<BroadPhasePair> candidatePairs;
    broadPhase.ComputePairs(scene, candidatePairs);

    // ========================================================================
    // Narrow Phase: finite collider pairs
    // ========================================================================
    // 現時点で実装済みの有限形状同士はSphere-Sphereだけです。
    // BoxのNarrow Phaseを追加したときは、このdispatchへBox-Box / Sphere-Boxを
    // 足すだけでBroad Phase自体はそのまま利用できます。
    for (const BroadPhasePair& pair : candidatePairs)
    {
        if (!scene.IsEntityAlive(pair.A) || !scene.IsEntityAlive(pair.B))
        {
            continue;
        }

        TransformComponent* transformA =
            scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());
        TransformComponent* transformB =
            scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
        ColliderComponent* colliderA =
            scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());
        ColliderComponent* colliderB =
            scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());

        if (transformA == nullptr || transformB == nullptr
            || colliderA == nullptr || colliderB == nullptr)
        {
            continue;
        }

        if (colliderA->Type == ColliderType::Sphere
            && colliderB->Type == ColliderType::Sphere)
        {
            ContactManifold manifold{};
            if (GenerateSphereSphereManifold(
                pair.A, *transformA, *colliderA,
                pair.B, *transformB, *colliderB,
                manifold))
            {
                m_Manifolds.push_back(manifold);
            }
        }
    }

    // ========================================================================
    // Infinite Plane
    // ========================================================================
    // Planeは無限形状なので有限AABBへ入れることができません。
    // そのためSphere-Planeだけは従来どおり別経路でNarrow Phaseを実行します。
    for (auto [sphereEntity, sphereTransform, sphereCollider]
        : scene.View<TransformComponent, ColliderComponent>())
    {
        if (sphereCollider.Type != ColliderType::Sphere)
        {
            continue;
        }

        for (auto [planeEntity, planeTransform, planeCollider]
            : scene.View<TransformComponent, ColliderComponent>())
        {
            if (planeCollider.Type != ColliderType::Plane
                || sphereEntity == planeEntity)
            {
                continue;
            }

            ContactManifold manifold{};
            if (GenerateSpherePlaneManifold(
                sphereEntity, sphereTransform, sphereCollider,
                planeEntity, planeTransform, planeCollider,
                manifold))
            {
                m_Manifolds.push_back(manifold);
            }
        }
    }
}

void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    for (ContactManifold& manifold : m_Manifolds)
    {
        SolveContactManifold(scene, manifold, dt);
    }
}

void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type != BodyType::Dynamic)
        {
            continue;
        }

        if (!rigidBody.AllowSleep)
        {
            WakeRigidBody(rigidBody);
            continue;
        }

        if (rigidBody.IsSleeping)
        {
            rigidBody.LinearVelocity = math::Vec3{};
            continue;
        }

        const float threshold = std::max(rigidBody.SleepThreshold, 0.0f);
        if (rigidBody.LinearVelocity.LengthSq() <= threshold * threshold)
        {
            rigidBody.SleepTimer += dt;
            const float requiredSleepTime =
                std::max(rigidBody.SleepTimeThreshold, 0.0f);

            if (rigidBody.SleepTimer >= requiredSleepTime)
            {
                rigidBody.IsSleeping = true;
                rigidBody.LinearVelocity = math::Vec3{};
                rigidBody.SleepTimer = requiredSleepTime;
            }
        }
        else
        {
            rigidBody.SleepTimer = 0.0f;
        }
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
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    RigidBodyComponent* rigidBody =
        scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr
        || rigidBody->Type != BodyType::Dynamic
        || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    rigidBody->Force += force;
}

void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    RigidBodyComponent* rigidBody =
        scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr
        || rigidBody->Type != BodyType::Dynamic
        || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
}

void PhysicsWorld::WakeUp(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    RigidBodyComponent* rigidBody =
        scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody != nullptr && rigidBody->Type == BodyType::Dynamic)
    {
        WakeRigidBody(*rigidBody);
    }
}

void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    // Semi-Implicit Euler -> Broad Phase -> Narrow Phase -> Solverの順です。
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
