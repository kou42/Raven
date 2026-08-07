#include <algorithm>
#include <cmath>
#include <limits>

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

bool RayCastSphere(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const math::Vec3& center, float radius, float& outFraction, math::Vec3& outNormal)
{
    const math::Vec3 m = origin - center;
    const float a = math::Vec3::Dot(direction, direction);
    if (a <= 1.0e-12f) return false;
    const float c = math::Vec3::Dot(m, m) - radius * radius;
    if (c <= 0.0f)
    {
        outFraction = 0.0f;
        const float lengthSq = m.LengthSq();
        outNormal = lengthSq > 1.0e-12f ? m / std::sqrt(lengthSq) : -direction.Normalized();
        return true;
    }
    const float b = math::Vec3::Dot(m, direction);
    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f) return false;
    const float fraction = (-b - std::sqrt(discriminant)) / a;
    if (fraction < 0.0f || fraction > maxFraction) return false;
    outFraction = fraction;
    outNormal = (origin + direction * fraction - center).Normalized();
    return true;
}

bool RayCastPlane(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const TransformComponent& transform, const ColliderComponent& collider,
    float& outFraction, math::Vec3& outNormal)
{
    math::Vec3 normal = collider.PlaneNormal.Normalized();
    if (normal.LengthSq() <= 1.0e-12f) return false;
    const math::Vec3 pointOnPlane = transform.Position + collider.Offset;
    const float denominator = math::Vec3::Dot(normal, direction);
    const float signedDistance = math::Vec3::Dot(normal, origin - pointOnPlane);
    if (std::abs(denominator) <= 1.0e-8f)
    {
        if (std::abs(signedDistance) > 1.0e-6f) return false;
        outFraction = 0.0f;
        outNormal = normal;
        return true;
    }
    const float fraction = -signedDistance / denominator;
    if (fraction < 0.0f || fraction > maxFraction) return false;
    outFraction = fraction;
    outNormal = denominator < 0.0f ? normal : -normal;
    return true;
}

bool RayCastCollider(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const TransformComponent& transform, const ColliderComponent& collider,
    float& outFraction, math::Vec3& outNormal)
{
    switch (collider.Type)
    {
    case ColliderType::Sphere:
        return RayCastSphere(origin, direction, maxFraction,
            transform.Position + collider.Offset, std::max(collider.Radius, 0.0f), outFraction, outNormal);
    case ColliderType::Box:
    {
        AABB bounds{};
        if (!ComputeColliderAABB(transform, collider, bounds)) return false;
        return bounds.RayCast(origin, direction, maxFraction, outFraction, &outNormal);
    }
    case ColliderType::Plane:
        return RayCastPlane(origin, direction, maxFraction, transform, collider, outFraction, outNormal);
    default:
        return false;
    }
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
        rigidBody.LinearVelocity *= 1.0f / (1.0f + std::max(rigidBody.LinearDamping, 0.0f) * dt);
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
    std::vector<BroadPhasePair> candidatePairs;
    m_BroadPhase.ComputePairs(scene, candidatePairs);

    for (const BroadPhasePair& pair : candidatePairs)
    {
        if (!scene.IsEntityAlive(pair.A) || !scene.IsEntityAlive(pair.B)) continue;
        TransformComponent* transformA = scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());
        TransformComponent* transformB = scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
        ColliderComponent* colliderA = scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());
        ColliderComponent* colliderB = scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());
        if (!transformA || !transformB || !colliderA || !colliderB) continue;

        ContactManifold manifold{};
        bool generated = false;

        if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereSphereManifold(pair.A, *transformA, *colliderA,
                pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateSphereBoxManifold(pair.A, *transformA, *colliderA,
                pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Sphere)
        {
            // Sphere-Box関数はA=Sphere/B=Box規約です。
            // Broad Phase pairの順番に関係なく同じ規約を保つため引数を反転します。
            generated = GenerateSphereBoxManifold(pair.B, *transformB, *colliderB,
                pair.A, *transformA, *colliderA, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Box)
        {
            // 現在のBoxはworld-axis alignedなので、3軸AABB SAT + face manifoldで解決します。
            generated = GenerateBoxBoxManifold(pair.A, *transformA, *colliderA,
                pair.B, *transformB, *colliderB, manifold);
        }

        if (generated) m_Manifolds.push_back(manifold);
    }

    // Planeは無限形状なのでDynamic Tree外でSphere-Planeを判定します。
    for (auto [sphereEntity, sphereTransform, sphereCollider] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (sphereCollider.Type != ColliderType::Sphere) continue;
        for (auto [planeEntity, planeTransform, planeCollider] : scene.View<TransformComponent, ColliderComponent>())
        {
            if (planeCollider.Type != ColliderType::Plane || sphereEntity == planeEntity) continue;
            ContactManifold manifold{};
            if (GenerateSpherePlaneManifold(sphereEntity, sphereTransform, sphereCollider,
                planeEntity, planeTransform, planeCollider, manifold))
                m_Manifolds.push_back(manifold);
        }
    }
}

bool PhysicsWorld::RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
    float maxFraction, PhysicsRayCastHit& outHit)
{
    if (maxFraction < 0.0f || direction.LengthSq() <= 1.0e-12f) return false;
    bool hasHit = false;
    float closestFraction = maxFraction;
    PhysicsRayCastHit closest{};

    m_BroadPhase.RayCast(scene, origin, direction, maxFraction,
        [&](Entity entity, uint32_t proxyId, float fatFraction, const math::Vec3& fatNormal, float currentMax) -> float
        {
            static_cast<void>(proxyId); static_cast<void>(fatFraction); static_cast<void>(fatNormal);
            const TransformComponent* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
            const ColliderComponent* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
            if (!transform || !collider || collider->Type == ColliderType::Plane) return currentMax;
            float fraction = 0.0f; math::Vec3 normal{};
            if (!RayCastCollider(origin, direction, currentMax, *transform, *collider, fraction, normal)) return currentMax;
            if (!hasHit || fraction < closestFraction)
            {
                hasHit = true; closestFraction = fraction;
                closest.HitEntity = entity; closest.Fraction = fraction;
                closest.Point = origin + direction * fraction; closest.Normal = normal;
            }
            return closestFraction;
        });

    for (auto [entity, transform, collider] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (collider.Type != ColliderType::Plane) continue;
        float fraction = 0.0f; math::Vec3 normal{};
        if (!RayCastPlane(origin, direction, closestFraction, transform, collider, fraction, normal)) continue;
        if (!hasHit || fraction < closestFraction)
        {
            hasHit = true; closestFraction = fraction;
            closest.HitEntity = entity; closest.Fraction = fraction;
            closest.Point = origin + direction * fraction; closest.Normal = normal;
        }
    }
    if (hasHit) outHit = closest;
    return hasHit;
}

void PhysicsWorld::QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities)
{
    outEntities.clear();
    if (!queryBounds.IsValid()) return;
    m_BroadPhase.QueryAABB(scene, queryBounds,
        [&](Entity entity, uint32_t proxyId) -> bool
        {
            static_cast<void>(proxyId);
            const TransformComponent* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
            const ColliderComponent* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
            if (!transform || !collider) return true;
            AABB tightBounds{};
            if (ComputeColliderAABB(*transform, *collider, tightBounds) && tightBounds.Overlaps(queryBounds))
                outEntities.push_back(entity);
            return true;
        });
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
    WakeRigidBody(*rigidBody); rigidBody->Force += force;
}

void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (!scene.IsEntityAlive(entity)) return;
    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f) return;
    WakeRigidBody(*rigidBody); rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
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
