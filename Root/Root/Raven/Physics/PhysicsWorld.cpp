#include <algorithm>
#include <cmath>
#include <limits>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Physics/Collision/BroadPhase.inl"
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

bool IsSamePair(const ContactManifold& a, const ContactManifold& b)
{
    return (a.A == b.A && a.B == b.B) || (a.A == b.B && a.B == b.A);
}

bool RayCastSphere(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const math::Vec3& center, float radius, float& outFraction, math::Vec3& outNormal)
{
    const math::Vec3 m = origin - center;
    const float a = math::Vec3::Dot(direction, direction);
    if (a <= 1e-12f) return false;

    const float c = math::Vec3::Dot(m, m) - radius * radius;
    if (c <= 0.0f)
    {
        outFraction = 0.0f;
        const float lengthSq = m.LengthSq();
        outNormal = lengthSq > 1e-12f ? m / std::sqrt(lengthSq) : -direction.Normalized();
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
    const TransformComponent& transform, const ColliderComponent& collider, float& outFraction,
    math::Vec3& outNormal)
{
    math::Vec3 normal = collider.PlaneNormal.Normalized();
    if (normal.LengthSq() <= 1e-12f) return false;

    const float denominator = math::Vec3::Dot(normal, direction);
    const float distance = math::Vec3::Dot(normal, origin - (transform.Position + collider.Offset));
    if (std::abs(denominator) <= 1e-8f)
    {
        if (std::abs(distance) > 1e-6f) return false;
        outFraction = 0.0f;
        outNormal = normal;
        return true;
    }

    const float fraction = -distance / denominator;
    if (fraction < 0.0f || fraction > maxFraction) return false;
    outFraction = fraction;
    outNormal = denominator < 0.0f ? normal : -normal;
    return true;
}

bool RayCastCollider(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const TransformComponent& transform, const ColliderComponent& collider, float& outFraction,
    math::Vec3& outNormal)
{
    if (collider.Type == ColliderType::Sphere)
    {
        return RayCastSphere(origin, direction, maxFraction, transform.Position + collider.Offset,
            std::max(collider.Radius, 0.0f), outFraction, outNormal);
    }
    if (collider.Type == ColliderType::Box)
    {
        AABB bounds{};
        return ComputeColliderAABB(transform, collider, bounds) &&
            bounds.RayCast(origin, direction, maxFraction, outFraction, &outNormal);
    }
    if (collider.Type == ColliderType::Plane)
    {
        return RayCastPlane(origin, direction, maxFraction, transform, collider, outFraction, outNormal);
    }
    return false;
}
} // namespace

void PhysicsWorld::SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
const math::Vec3& PhysicsWorld::GetGravity() const { return m_Gravity; }

void PhysicsWorld::ApplyForces(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody, collider] :
        scene.View<TransformComponent, RigidBodyComponent, ColliderComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping || rigidBody.InverseMass <= 0.0f)
            continue;

        math::Vec3 acceleration{};
        if (rigidBody.UseGravity) acceleration += m_Gravity;
        acceleration += rigidBody.Force * rigidBody.InverseMass;
        rigidBody.LinearVelocity += acceleration * dt;

        // ====================================================================
        // Torque -> Angular acceleration
        // ====================================================================
        // Torqueはworld-spaceで蓄積しています。剛体の現在姿勢を反映した逆慣性テンソル
        // I_world^-1 を掛けて角加速度を求め、Linearと同じsemi-implicit順序で
        // AngularVelocityへ積分します。
        EnsurePhysicsOrientation(transform, rigidBody);
        const math::Mat3 inverseInertia = ComputeWorldInverseInertia(&transform, &rigidBody, &collider);
        rigidBody.AngularVelocity += (inverseInertia * rigidBody.Torque) * dt;
    }
}

void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping) continue;

        rigidBody.LinearVelocity *= 1.0f /
            (1.0f + std::max(rigidBody.LinearDamping, 0.0f) * dt);
        rigidBody.AngularVelocity *= 1.0f /
            (1.0f + std::max(rigidBody.AngularDamping, 0.0f) * dt);
    }
}

void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type == BodyType::Static ||
            (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping))
            continue;

        transform.Position += rigidBody.LinearVelocity * dt;

        // PhysicsではQuaternionを正規姿勢として積分し、最後に既存Renderer互換の
        // Transform::Rotation(Euler)へ同期します。これによりOBB / 慣性テンソル / 描画が
        // 同じ姿勢を共有できます。
        IntegratePhysicsOrientation(transform, rigidBody, dt);
    }
}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    m_PreviousManifolds = std::move(m_Manifolds);
    m_Manifolds.clear();

    std::vector<BroadPhasePair> pairs;
    m_BroadPhase.ComputePairs(scene, pairs);

    for (const auto& pair : pairs)
    {
        if (!scene.IsEntityAlive(pair.A) || !scene.IsEntityAlive(pair.B)) continue;

        auto* transformA = scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());
        auto* transformB = scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
        auto* colliderA = scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());
        auto* colliderB = scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());
        if (!transformA || !transformB || !colliderA || !colliderB) continue;

        ContactManifold manifold{};
        bool generated = false;
        if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereSphereManifold(
                pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateSphereBoxManifold(
                pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereBoxManifold(
                pair.B, *transformB, *colliderB, pair.A, *transformA, *colliderA, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateBoxBoxManifold(
                pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }
        if (generated) m_Manifolds.push_back(manifold);
    }

    for (auto [sphereEntity, sphereTransform, sphereCollider] :
        scene.View<TransformComponent, ColliderComponent>())
    {
        if (sphereCollider.Type != ColliderType::Sphere) continue;
        for (auto [planeEntity, planeTransform, planeCollider] :
            scene.View<TransformComponent, ColliderComponent>())
        {
            if (planeCollider.Type != ColliderType::Plane || sphereEntity == planeEntity) continue;
            ContactManifold manifold{};
            if (GenerateSpherePlaneManifold(
                    sphereEntity, sphereTransform, sphereCollider, planeEntity, planeTransform,
                    planeCollider, manifold))
                m_Manifolds.push_back(manifold);
        }
    }

    RestorePersistentContacts();
    m_SolverDebugStatistics.ManifoldCount = static_cast<uint32_t>(m_Manifolds.size());
    for (const ContactManifold& manifold : m_Manifolds)
    {
        m_SolverDebugStatistics.ContactPointCount += static_cast<uint32_t>(manifold.PointCount);
        for (std::size_t i = 0; i < manifold.PointCount; ++i)
        {
            m_SolverDebugStatistics.MaxPenetration = std::max(
                m_SolverDebugStatistics.MaxPenetration,
                std::max(manifold.Points[i].Penetration, 0.0f));
        }
    }
}

void PhysicsWorld::RestorePersistentContacts()
{
    constexpr float matchDistanceSq = 0.05f * 0.05f;
    constexpr float normalThreshold = 0.9f;

    for (auto& current : m_Manifolds)
    {
        if (current.IsTrigger || current.PointCount == 0) continue;

        const ContactManifold* previous = nullptr;
        for (const auto& candidate : m_PreviousManifolds)
        {
            if (!candidate.IsTrigger && candidate.PointCount > 0 && IsSamePair(current, candidate))
            {
                const bool sameOrder = current.A == candidate.A && current.B == candidate.B;
                const math::Vec3 previousNormal = sameOrder ? candidate.Normal : -candidate.Normal;
                if (math::Vec3::Dot(current.Normal.Normalized(), previousNormal.Normalized()) >= normalThreshold)
                {
                    previous = &candidate;
                    break;
                }
            }
        }
        if (!previous) continue;

        ++m_SolverDebugStatistics.PersistentManifoldCount;
        const bool sameOrder = current.A == previous->A && current.B == previous->B;
        bool used[ContactManifold::MaxContactPointCount]{};

        for (std::size_t i = 0; i < current.PointCount; ++i)
        {
            std::size_t bestIndex = ContactManifold::MaxContactPointCount;
            float bestDistanceSq = matchDistanceSq;
            for (std::size_t j = 0; j < previous->PointCount; ++j)
            {
                if (used[j]) continue;
                const float distanceSq =
                    (current.Points[i].Position - previous->Points[j].Position).LengthSq();
                if (distanceSq <= bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    bestIndex = j;
                }
            }
            if (bestIndex == ContactManifold::MaxContactPointCount) continue;

            used[bestIndex] = true;
            const auto& source = previous->Points[bestIndex];
            auto& target = current.Points[i];
            target.AccumulatedNormalImpulse = source.AccumulatedNormalImpulse;
            target.AccumulatedTangentImpulse = sameOrder ? source.AccumulatedTangentImpulse
                                                         : -source.AccumulatedTangentImpulse;
            target.CachedTangent = sameOrder ? source.CachedTangent : -source.CachedTangent;
            ++m_SolverDebugStatistics.PersistentContactPointCount;
        }

        if (current.Points[0].AccumulatedNormalImpulse == 0.0f && previous->PointCount > 0)
        {
            current.Points[0].AccumulatedNormalImpulse = previous->Points[0].AccumulatedNormalImpulse;
            current.Points[0].AccumulatedTangentImpulse =
                sameOrder ? previous->Points[0].AccumulatedTangentImpulse
                          : -previous->Points[0].AccumulatedTangentImpulse;
            current.Points[0].CachedTangent = sameOrder ? previous->Points[0].CachedTangent
                                                        : -previous->Points[0].CachedTangent;
        }
    }
}

bool PhysicsWorld::RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
    float maxFraction, PhysicsRayCastHit& outHit)
{
    if (maxFraction < 0.0f || direction.LengthSq() <= 1e-12f) return false;

    bool hit = false;
    float closestFraction = maxFraction;
    PhysicsRayCastHit result{};
    m_BroadPhase.RayCast(scene, origin, direction, maxFraction,
        [&](Entity entity, uint32_t proxyIndex, float fraction, const math::Vec3& normal,
            float currentClosest) -> float
        {
            static_cast<void>(proxyIndex);
            static_cast<void>(fraction);
            static_cast<void>(normal);
            auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
            auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
            if (!transform || !collider || collider->Type == ColliderType::Plane) return currentClosest;

            float candidateFraction = 0.0f;
            math::Vec3 candidateNormal{};
            if (!RayCastCollider(origin, direction, currentClosest, *transform, *collider,
                    candidateFraction, candidateNormal))
                return currentClosest;

            if (!hit || candidateFraction < closestFraction)
            {
                hit = true;
                closestFraction = candidateFraction;
                result.HitEntity = entity;
                result.Fraction = candidateFraction;
                result.Point = origin + direction * candidateFraction;
                result.Normal = candidateNormal;
            }
            return closestFraction;
        });

    for (auto [entity, transform, collider] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (collider.Type != ColliderType::Plane) continue;
        float candidateFraction = 0.0f;
        math::Vec3 candidateNormal{};
        if (RayCastPlane(origin, direction, closestFraction, transform, collider,
                candidateFraction, candidateNormal) && (!hit || candidateFraction < closestFraction))
        {
            hit = true;
            closestFraction = candidateFraction;
            result.HitEntity = entity;
            result.Fraction = candidateFraction;
            result.Point = origin + direction * candidateFraction;
            result.Normal = candidateNormal;
        }
    }
    if (hit) outHit = result;
    return hit;
}

void PhysicsWorld::QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities)
{
    outEntities.clear();
    if (!queryBounds.IsValid()) return;

    m_BroadPhase.QueryAABB(scene, queryBounds, [&](Entity entity, uint32_t proxyIndex) -> bool
    {
        static_cast<void>(proxyIndex);
        auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
        auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
        if (!transform || !collider) return true;

        AABB bounds{};
        if (ComputeColliderAABB(*transform, *collider, bounds) && bounds.Overlaps(queryBounds))
            outEntities.push_back(entity);
        return true;
    });
}

void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    if (m_SolverSettings.EnableWarmStart)
    {
        for (const ContactManifold& manifold : m_Manifolds)
        {
            for (std::size_t i = 0; i < manifold.PointCount; ++i)
            {
                if (!manifold.IsTrigger &&
                    (manifold.Points[i].AccumulatedNormalImpulse > 0.0f ||
                     std::abs(manifold.Points[i].AccumulatedTangentImpulse) > 1.0e-8f))
                {
                    ++m_SolverDebugStatistics.WarmStartedConstraintCount;
                }
            }
        }
    }

    m_SolverDebugStatistics.VelocityIterations = std::max(m_SolverSettings.VelocityIterations, 1u);
    SolveContactManifolds(scene, m_Manifolds, dt, m_SolverSettings);
    UpdateSolverDebugStatisticsAfterSolve();
}

void PhysicsWorld::UpdateSolverDebugStatisticsAfterSolve()
{
    for (const ContactManifold& manifold : m_Manifolds)
    {
        for (std::size_t i = 0; i < manifold.PointCount; ++i)
        {
            const ContactPoint& point = manifold.Points[i];
            m_SolverDebugStatistics.MaxNormalImpulse = std::max(
                m_SolverDebugStatistics.MaxNormalImpulse,
                std::max(point.AccumulatedNormalImpulse, 0.0f));
            m_SolverDebugStatistics.MaxFrictionImpulse = std::max(
                m_SolverDebugStatistics.MaxFrictionImpulse,
                std::abs(point.AccumulatedTangentImpulse));
        }
    }
}

void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        if (rigidBody.Type != BodyType::Dynamic) continue;
        if (!rigidBody.AllowSleep)
        {
            WakeRigidBody(rigidBody);
            continue;
        }
        if (rigidBody.IsSleeping)
        {
            rigidBody.LinearVelocity = math::Vec3{};
            rigidBody.AngularVelocity = math::Vec3{};
            continue;
        }

        const float linearThreshold = std::max(rigidBody.SleepThreshold, 0.0f);
        const float angularThreshold = std::max(rigidBody.AngularSleepThreshold, 0.0f);
        const bool linearStill = rigidBody.LinearVelocity.LengthSq() <= linearThreshold * linearThreshold;
        const bool angularStill = rigidBody.AngularVelocity.LengthSq() <= angularThreshold * angularThreshold;

        // 並進だけ静止していても回転中ならSleepさせません。
        if (linearStill && angularStill)
        {
            rigidBody.SleepTimer += dt;
            const float requiredTime = std::max(rigidBody.SleepTimeThreshold, 0.0f);
            if (rigidBody.SleepTimer >= requiredTime)
            {
                rigidBody.IsSleeping = true;
                rigidBody.LinearVelocity = math::Vec3{};
                rigidBody.AngularVelocity = math::Vec3{};
                rigidBody.SleepTimer = requiredTime;
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
    if (!scene.IsEntityAlive(entity)) return;
    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f) return;
    WakeRigidBody(*rigidBody);
    rigidBody->Force += force;
}

void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (!scene.IsEntityAlive(entity)) return;
    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f) return;
    WakeRigidBody(*rigidBody);
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
}

void PhysicsWorld::WakeUp(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity)) return;
    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody && rigidBody->Type == BodyType::Dynamic) WakeRigidBody(*rigidBody);
}

void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f) return;
    m_SolverDebugStatistics.Reset();
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
