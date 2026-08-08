#include <algorithm>
#include <cmath>

#include "Raven/Physics/RigidBodyDynamics.h"
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
    ColliderComponent* ColliderA = nullptr;
    ColliderComponent* ColliderB = nullptr;
    float InverseMassA = 0.0f;
    float InverseMassB = 0.0f;
    float InverseMassSum = 0.0f;
    math::Mat3 InverseInertiaA{};
    math::Mat3 InverseInertiaB{};
};

bool GetSolverBodies(Scene& scene, const ContactManifold& manifold, SolverBodies& out)
{
    if (!scene.IsEntityAlive(manifold.A) || !scene.IsEntityAlive(manifold.B)) return false;
    out.TransformA = scene.TryGetComponent<TransformComponent>(manifold.A.GetIndex());
    out.TransformB = scene.TryGetComponent<TransformComponent>(manifold.B.GetIndex());
    if (!out.TransformA || !out.TransformB) return false;
    out.BodyA = scene.TryGetComponent<RigidBodyComponent>(manifold.A.GetIndex());
    out.BodyB = scene.TryGetComponent<RigidBodyComponent>(manifold.B.GetIndex());
    out.ColliderA = scene.TryGetComponent<ColliderComponent>(manifold.A.GetIndex());
    out.ColliderB = scene.TryGetComponent<ColliderComponent>(manifold.B.GetIndex());
    out.InverseMassA = GetInverseMass(out.BodyA);
    out.InverseMassB = GetInverseMass(out.BodyB);
    out.InverseMassSum = out.InverseMassA + out.InverseMassB;
    out.InverseInertiaA = ComputeWorldInverseInertia(out.TransformA, out.BodyA, out.ColliderA);
    out.InverseInertiaB = ComputeWorldInverseInertia(out.TransformB, out.BodyB, out.ColliderB);
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

void ApplyImpulseAtPoint(
    SolverBodies& bodies,
    const math::Vec3& worldPoint,
    const math::Vec3& impulse)
{
    // ========================================================================
    // Linear + Angular impulse
    // ========================================================================
    // Jを接触点へ加えると重心速度だけでなく、重心から接触点までの腕 r により
    // torque impulse r x J が発生します。
    // Aには-J、Bには+Jを適用するため符号もそれぞれ反転します。
    if (bodies.BodyA && bodies.InverseMassA > 0.0f)
    {
        WakeIfSleeping(bodies.BodyA);
        bodies.BodyA->LinearVelocity -= impulse * bodies.InverseMassA;
        const math::Vec3 rA = worldPoint - bodies.TransformA->Position;
        bodies.BodyA->AngularVelocity -= bodies.InverseInertiaA * math::Vec3::Cross(rA, impulse);
    }
    if (bodies.BodyB && bodies.InverseMassB > 0.0f)
    {
        WakeIfSleeping(bodies.BodyB);
        bodies.BodyB->LinearVelocity += impulse * bodies.InverseMassB;
        const math::Vec3 rB = worldPoint - bodies.TransformB->Position;
        bodies.BodyB->AngularVelocity += bodies.InverseInertiaB * math::Vec3::Cross(rB, impulse);
    }
}

float ComputeEffectiveMassDenominator(
    const SolverBodies& bodies,
    const math::Vec3& worldPoint,
    const math::Vec3& direction)
{
    const math::Vec3 rA = worldPoint - bodies.TransformA->Position;
    const math::Vec3 rB = worldPoint - bodies.TransformB->Position;

    // k = invMassA + invMassB
    //   + n dot ((I_A^-1 (rA x n)) x rA)
    //   + n dot ((I_B^-1 (rB x n)) x rB)
    //
    // この角運動項がないと、Boxの端を叩いても中心を叩いても同じImpulse量になり、
    // 回転にエネルギーが分配されません。
    const math::Vec3 angularA = math::Vec3::Cross(
        bodies.InverseInertiaA * math::Vec3::Cross(rA, direction), rA);
    const math::Vec3 angularB = math::Vec3::Cross(
        bodies.InverseInertiaB * math::Vec3::Cross(rB, direction), rB);

    return bodies.InverseMassSum
        + math::Vec3::Dot(direction, angularA + angularB);
}

void ApplyPositionCorrection(Scene& scene, ContactManifold& manifold,
    const ContactSolverSettings& settings)
{
    if (manifold.IsTrigger || manifold.PointCount == 0) return;
    SolverBodies bodies{}; math::Vec3 normal{};
    if (!GetSolverBodies(scene, manifold, bodies) || !GetNormal(manifold, normal)) return;

    // Position correctionは当面、従来どおり重心の並進だけで処理します。
    // 姿勢まで直接補正するとvelocity solverとの二重補正が起きやすいため、
    // Angular Position Correctionは後のsplit impulse/position solver段階で導入します。
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

    // OBBでは複数接触点がそれぞれ異なる腕 r を持つため、全Contact Pointを
    // 独立ConstraintとしてWarm Startします。
    for (std::size_t i = 0; i < manifold.PointCount; ++i)
    {
        ContactPoint& point = manifold.Points[i];
        math::Vec3 impulse = normal * std::max(point.AccumulatedNormalImpulse, 0.0f);
        const float tangentLengthSquared = point.CachedTangent.LengthSq();
        if (tangentLengthSquared > 1.0e-12f)
        {
            const math::Vec3 tangent = point.CachedTangent / std::sqrt(tangentLengthSquared);
            impulse += tangent * point.AccumulatedTangentImpulse;
        }
        ApplyImpulseAtPoint(bodies, point.Position, impulse);
    }
}

void SolvePointVelocityConstraint(
    SolverBodies& bodies,
    ContactManifold& manifold,
    ContactPoint& constraint,
    const math::Vec3& normal,
    const ContactSolverSettings& settings)
{
    math::Vec3 relativeVelocity =
        GetVelocityAtPoint(bodies.BodyB, bodies.TransformB, constraint.Position)
        - GetVelocityAtPoint(bodies.BodyA, bodies.TransformA, constraint.Position);
    const float velocityAlongNormal = math::Vec3::Dot(relativeVelocity, normal);

    float restitution = 0.0f;
    if (velocityAlongNormal < -std::max(settings.RestitutionVelocityThreshold, 0.0f))
        restitution = std::clamp(manifold.Restitution, 0.0f, 1.0f);

    const float normalDenominator = ComputeEffectiveMassDenominator(bodies, constraint.Position, normal);
    if (normalDenominator <= 1.0e-12f) return;

    const float deltaNormalImpulse =
        -(1.0f + restitution) * velocityAlongNormal / normalDenominator;
    const float oldNormalImpulse = constraint.AccumulatedNormalImpulse;
    constraint.AccumulatedNormalImpulse = std::max(oldNormalImpulse + deltaNormalImpulse, 0.0f);
    const float appliedNormalImpulse = constraint.AccumulatedNormalImpulse - oldNormalImpulse;
    ApplyImpulseAtPoint(bodies, constraint.Position, normal * appliedNormalImpulse);

    // Normal impulseで角速度も変化したため、摩擦を解く前に接触点速度を再計算します。
    relativeVelocity =
        GetVelocityAtPoint(bodies.BodyB, bodies.TransformB, constraint.Position)
        - GetVelocityAtPoint(bodies.BodyA, bodies.TransformA, constraint.Position);
    math::Vec3 tangent = relativeVelocity - normal * math::Vec3::Dot(relativeVelocity, normal);
    const float tangentLengthSquared = tangent.LengthSq();
    if (tangentLengthSquared <= 1.0e-12f) return;
    tangent /= std::sqrt(tangentLengthSquared);
    constraint.CachedTangent = tangent;

    const float tangentDenominator = ComputeEffectiveMassDenominator(bodies, constraint.Position, tangent);
    if (tangentDenominator <= 1.0e-12f) return;

    const float deltaTangentImpulse =
        -math::Vec3::Dot(relativeVelocity, tangent) / tangentDenominator;
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
    ApplyImpulseAtPoint(bodies, constraint.Position,
        tangent * (newTangentImpulse - oldTangentImpulse));
}

void SolveVelocityConstraint(Scene& scene, ContactManifold& manifold,
    const ContactSolverSettings& settings)
{
    if (manifold.IsTrigger || manifold.PointCount == 0) return;
    SolverBodies bodies{}; math::Vec3 normal{};
    if (!GetSolverBodies(scene, manifold, bodies) || !GetNormal(manifold, normal)) return;

    // Sequential ImpulseなのでManifold内も1点ずつGauss-Seidel式に更新します。
    // 先に解いた点で変化したlinear/angular velocityを次の点が即座に参照するため、
    // face-faceの4点接触でも回転拘束が自然に伝播します。
    for (std::size_t i = 0; i < manifold.PointCount; ++i)
    {
        SolvePointVelocityConstraint(bodies, manifold, manifold.Points[i], normal, settings);
    }
}

} // namespace

void SolveContactManifolds(Scene& scene, std::vector<ContactManifold>& manifolds,
    float dt, const ContactSolverSettings& settings)
{
    if (dt <= 0.0f) return;

    for (ContactManifold& manifold : manifolds)
        ApplyPositionCorrection(scene, manifold, settings);

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
