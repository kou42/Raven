#include <algorithm>
#include <cmath>

#include "Raven/Physics/Solver/ContactSolver.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace ph
{
namespace
{
float GetInverseMass(const RigidBodyComponent* rigidBody)
{
    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic) return 0.0f;
    return std::max(rigidBody->InverseMass, 0.0f);
}

math::Vec3 GetLinearVelocity(const RigidBodyComponent* rigidBody)
{
    return rigidBody != nullptr ? rigidBody->LinearVelocity : math::Vec3{};
}

void WakeIfSleeping(RigidBodyComponent* rigidBody)
{
    if (rigidBody != nullptr && rigidBody->Type == BodyType::Dynamic && rigidBody->IsSleeping)
    {
        rigidBody->IsSleeping = false;
        rigidBody->SleepTimer = 0.0f;
    }
}

struct SolverBodies
{
    TransformComponent* TransformA = nullptr;
    TransformComponent* TransformB = nullptr;
    RigidBodyComponent* BodyA = nullptr;
    RigidBodyComponent* BodyB = nullptr;
    float InverseMassA = 0.0f;
    float InverseMassB = 0.0f;
    float InverseMassSum = 0.0f;
};

bool GetSolverBodies(Scene& scene, const ContactManifold& manifold, SolverBodies& out)
{
    if (!scene.IsEntityAlive(manifold.A) || !scene.IsEntityAlive(manifold.B)) return false;
    out.TransformA = scene.TryGetComponent<TransformComponent>(manifold.A.GetIndex());
    out.TransformB = scene.TryGetComponent<TransformComponent>(manifold.B.GetIndex());
    if (!out.TransformA || !out.TransformB) return false;
    out.BodyA = scene.TryGetComponent<RigidBodyComponent>(manifold.A.GetIndex());
    out.BodyB = scene.TryGetComponent<RigidBodyComponent>(manifold.B.GetIndex());
    out.InverseMassA = GetInverseMass(out.BodyA);
    out.InverseMassB = GetInverseMass(out.BodyB);
    out.InverseMassSum = out.InverseMassA + out.InverseMassB;
    return out.InverseMassSum > 0.0f;
}

bool GetNormal(const ContactManifold& manifold, math::Vec3& outNormal)
{
    const float lengthSquared = manifold.Normal.LengthSq();
    if (lengthSquared <= 1.0e-12f) return false;
    outNormal = manifold.Normal / std::sqrt(lengthSquared);
    return true;
}

float GetMaximumPenetration(const ContactManifold& manifold)
{
    float penetration = 0.0f;
    const std::size_t count = std::min(manifold.PointCount, ContactManifold::MaxContactPointCount);
    for (std::size_t i = 0; i < count; ++i)
        penetration = std::max(penetration, std::max(manifold.Points[i].Penetration, 0.0f));
    return penetration;
}

void ApplyImpulse(RigidBodyComponent* a, RigidBodyComponent* b,
    float invA, float invB, const math::Vec3& impulse)
{
    if (a && invA > 0.0f) { WakeIfSleeping(a); a->LinearVelocity -= impulse * invA; }
    if (b && invB > 0.0f) { WakeIfSleeping(b); b->LinearVelocity += impulse * invB; }
}

void ApplyPositionCorrection(Scene& scene, ContactManifold& manifold,
    const ContactSolverSettings& settings)
{
    if (manifold.IsTrigger || manifold.PointCount == 0) return;
    SolverBodies bodies{}; math::Vec3 normal{};
    if (!GetSolverBodies(scene, manifold, bodies) || !GetNormal(manifold, normal)) return;

    const float correctionMagnitude =
        std::max(GetMaximumPenetration(manifold) - std::max(settings.PenetrationSlop, 0.0f), 0.0f)
        * std::clamp(settings.PositionCorrectionPercent, 0.0f, 1.0f) / bodies.InverseMassSum;
    const math::Vec3 correction = normal * correctionMagnitude;
    bodies.TransformA->Position -= correction * bodies.InverseMassA;
    bodies.TransformB->Position += correction * bodies.InverseMassB;
}

void WarmStartConstraint(Scene& scene, ContactManifold& manifold)
{
    if (manifold.IsTrigger || manifold.PointCount == 0) return;
    SolverBodies bodies{}; math::Vec3 normal{};
    if (!GetSolverBodies(scene, manifold, bodies) || !GetNormal(manifold, normal)) return;

    // Rotation未対応の現在はPoint[0]がManifold代表Constraintです。
    ContactPoint& point = manifold.Points[0];
    math::Vec3 impulse = normal * std::max(point.AccumulatedNormalImpulse, 0.0f);

    // CachedTangentは前Stepで実際に摩擦Constraintへ使用した単位接線です。
    // 接線が有効な場合のみ摩擦ImpulseもWarm Startします。
    const float tangentLengthSquared = point.CachedTangent.LengthSq();
    if (tangentLengthSquared > 1.0e-12f)
    {
        const math::Vec3 tangent = point.CachedTangent / std::sqrt(tangentLengthSquared);
        impulse += tangent * point.AccumulatedTangentImpulse;
    }

    ApplyImpulse(bodies.BodyA, bodies.BodyB,
        bodies.InverseMassA, bodies.InverseMassB, impulse);
}

void SolveVelocityConstraint(Scene& scene, ContactManifold& manifold,
    const ContactSolverSettings& settings)
{
    if (manifold.IsTrigger || manifold.PointCount == 0) return;
    SolverBodies bodies{}; math::Vec3 normal{};
    if (!GetSolverBodies(scene, manifold, bodies) || !GetNormal(manifold, normal)) return;

    ContactPoint& constraint = manifold.Points[0];
    math::Vec3 relativeVelocity = GetLinearVelocity(bodies.BodyB) - GetLinearVelocity(bodies.BodyA);
    const float velocityAlongNormal = math::Vec3::Dot(relativeVelocity, normal);

    float restitution = 0.0f;
    if (velocityAlongNormal < -std::max(settings.RestitutionVelocityThreshold, 0.0f))
        restitution = std::clamp(manifold.Restitution, 0.0f, 1.0f);

    const float deltaNormalImpulse =
        -(1.0f + restitution) * velocityAlongNormal / bodies.InverseMassSum;
    const float oldNormalImpulse = constraint.AccumulatedNormalImpulse;
    constraint.AccumulatedNormalImpulse = std::max(oldNormalImpulse + deltaNormalImpulse, 0.0f);
    const float appliedNormalImpulse = constraint.AccumulatedNormalImpulse - oldNormalImpulse;
    ApplyImpulse(bodies.BodyA, bodies.BodyB, bodies.InverseMassA, bodies.InverseMassB,
        normal * appliedNormalImpulse);

    relativeVelocity = GetLinearVelocity(bodies.BodyB) - GetLinearVelocity(bodies.BodyA);
    math::Vec3 tangent = relativeVelocity - normal * math::Vec3::Dot(relativeVelocity, normal);
    const float tangentLengthSquared = tangent.LengthSq();
    if (tangentLengthSquared <= 1.0e-12f) return;
    tangent /= std::sqrt(tangentLengthSquared);
    constraint.CachedTangent = tangent;

    const float deltaTangentImpulse =
        -math::Vec3::Dot(relativeVelocity, tangent) / bodies.InverseMassSum;
    const float staticFriction = std::max(manifold.StaticFriction, 0.0f);
    const float dynamicFriction = std::max(manifold.DynamicFriction, 0.0f);
    const float oldTangentImpulse = constraint.AccumulatedTangentImpulse;
    const float candidate = oldTangentImpulse + deltaTangentImpulse;
    const float staticLimit = constraint.AccumulatedNormalImpulse * staticFriction;

    float newTangentImpulse = 0.0f;
    if (std::abs(candidate) <= staticLimit)
        newTangentImpulse = candidate;
    else
    {
        const float dynamicLimit = constraint.AccumulatedNormalImpulse * dynamicFriction;
        newTangentImpulse = std::clamp(candidate, -dynamicLimit, dynamicLimit);
    }

    constraint.AccumulatedTangentImpulse = newTangentImpulse;
    ApplyImpulse(bodies.BodyA, bodies.BodyB, bodies.InverseMassA, bodies.InverseMassB,
        tangent * (newTangentImpulse - oldTangentImpulse));
}

} // namespace

void SolveContactManifolds(Scene& scene, std::vector<ContactManifold>& manifolds,
    float dt, const ContactSolverSettings& settings)
{
    if (dt <= 0.0f) return;

    // Accumulated ImpulseはPhysicsWorldのContact Persistenceによって既に前Stepから
    // 移植されています。ここでゼロクリアしてはいけません。
    for (ContactManifold& manifold : manifolds)
        ApplyPositionCorrection(scene, manifold, settings);

    // Warm Startはiteration 0より前に一度だけ行います。
    // 前Stepの収束解に近い速度状態から開始できるため、少ないiterationでも
    // 積み重ねや静止摩擦が安定します。
    if (settings.EnableWarmStart)
    {
        for (ContactManifold& manifold : manifolds)
            WarmStartConstraint(scene, manifold);
    }

    const uint32_t iterationCount = std::max(settings.VelocityIterations, 1u);
    for (uint32_t iteration = 0; iteration < iterationCount; ++iteration)
    {
        for (ContactManifold& manifold : manifolds)
            SolveVelocityConstraint(scene, manifold, settings);
    }
}

void SolveContactManifold(Scene& scene, ContactManifold& manifold, float dt)
{
    std::vector<ContactManifold> singleManifold{ manifold };
    SolveContactManifolds(scene, singleManifold, dt);
    manifold = singleManifold.front();
}

} // namespace ph
} // namespace Raven
