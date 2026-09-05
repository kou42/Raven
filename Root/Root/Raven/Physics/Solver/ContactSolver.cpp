#include <algorithm>
#include <cmath>

#include "Raven/Core/CPUProfiler.h"
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
// Contact Solver設計:
// - Velocity段で反発/摩擦を解く
// - Position段で残留貫通を補正
// - Warm Startで前フレーム解を初期値として収束を高速化
float GetInverseMass(const RigidBodyComponent* body)
{
    if (body == nullptr || body->Type != BodyType::Dynamic)
    {
        return 0.0f;
    }

    return std::max(body->InverseMass, 0.0f);
}

void WakeIfSleeping(RigidBodyComponent* body)
{
    if (body != nullptr
        && body->Type == BodyType::Dynamic
        && body->IsSleeping)
    {
        body->IsSleeping = false;
        body->SleepTimer = 0.0f;
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

// マニホールド解法に必要なTransform/剛体/慣性情報を1か所で収集します。
bool GetSolverBodies(Scene& scene, const ContactManifold& manifold, SolverBodies& outBodies)
{
    if (scene.IsEntityAlive(manifold.A) == false || scene.IsEntityAlive(manifold.B) == false)
    {
        return false;
    }

    outBodies.TransformA = scene.TryGetComponent<TransformComponent>(manifold.A.GetIndex());
    outBodies.TransformB = scene.TryGetComponent<TransformComponent>(manifold.B.GetIndex());
    if (outBodies.TransformA == nullptr || outBodies.TransformB == nullptr)
    {
        return false;
    }

    outBodies.BodyA = scene.TryGetComponent<RigidBodyComponent>(manifold.A.GetIndex());
    outBodies.BodyB = scene.TryGetComponent<RigidBodyComponent>(manifold.B.GetIndex());
    outBodies.ColliderA = scene.TryGetComponent<ColliderComponent>(manifold.A.GetIndex());
    outBodies.ColliderB = scene.TryGetComponent<ColliderComponent>(manifold.B.GetIndex());

    outBodies.InverseMassA = GetInverseMass(outBodies.BodyA);
    outBodies.InverseMassB = GetInverseMass(outBodies.BodyB);
    outBodies.InverseMassSum = outBodies.InverseMassA + outBodies.InverseMassB;

    outBodies.InverseInertiaA = ComputeWorldInverseInertia(
        outBodies.TransformA,
        outBodies.BodyA,
        outBodies.ColliderA);
    outBodies.InverseInertiaB = ComputeWorldInverseInertia(
        outBodies.TransformB,
        outBodies.BodyB,
        outBodies.ColliderB);

    return outBodies.InverseMassSum > 0.0f;
}

bool GetNormal(const ContactManifold& manifold, math::Vec3& outNormal)
{
    // 正規化不能な法線は制約として不正なので棄却します。
    const float lengthSq = manifold.Normal.LengthSq();
    if (lengthSq <= 1.0e-12f)
    {
        return false;
    }

    outNormal = manifold.Normal / std::sqrt(lengthSq);
    return true;
}

math::Vec3 RotateByOrientation(
    const RigidBodyComponent* body,
    const TransformComponent* transform,
    const math::Vec3& local)
{
    if (body != nullptr && body->OrientationInitialized)
    {
        return body->Orientation.Rotate(local);
    }

    // Static等でOrientation未初期化でも、Transform姿勢と一致した回転を使います。
    const math::Quat orientation = PhysicsOrientationFromEuler(transform->Rotation);
    return orientation.Rotate(local);
}

math::Vec3 InverseRotateByOrientation(
    const RigidBodyComponent* body,
    const TransformComponent* transform,
    const math::Vec3& world)
{
    math::Quat orientation = (body != nullptr && body->OrientationInitialized)
        ? body->Orientation
        : PhysicsOrientationFromEuler(transform->Rotation);
    orientation = orientation.Normalized();
    return orientation.Conjugate().Rotate(world);
}

// Position Solverで使う接触アンカーをローカル座標へ固定し、反復ごとの誤差蓄積を抑えます。
void InitializePositionAnchors(SolverBodies& bodies, ContactManifold& manifold)
{
    // 初回だけローカルアンカーと初期separationを固定し、反復中の再投影を安定化します。
    for (std::size_t i = 0; i < manifold.PointCount; ++i)
    {
        ContactPoint& point = manifold.Points[i];
        if (point.PositionAnchorsInitialized)
        {
            continue;
        }

        point.LocalAnchorA = InverseRotateByOrientation(
            bodies.BodyA,
            bodies.TransformA,
            point.Position - bodies.TransformA->Position);
        point.LocalAnchorB = InverseRotateByOrientation(
            bodies.BodyB,
            bodies.TransformB,
            point.Position - bodies.TransformB->Position);
        point.InitialSeparation = -std::max(point.Penetration, 0.0f);
        point.PositionAnchorsInitialized = true;
    }
}

math::Vec3 WorldAnchorA(const SolverBodies& bodies, const ContactPoint& point)
{
    return bodies.TransformA->Position
        + RotateByOrientation(bodies.BodyA, bodies.TransformA, point.LocalAnchorA);
}

math::Vec3 WorldAnchorB(const SolverBodies& bodies, const ContactPoint& point)
{
    return bodies.TransformB->Position
        + RotateByOrientation(bodies.BodyB, bodies.TransformB, point.LocalAnchorB);
}

// 線形・角速度の両方へインパルスを反映し、接触点まわりの回転効果も含めて更新します。
void ApplyImpulseAtPoint(
    SolverBodies& bodies,
    const math::Vec3& point,
    const math::Vec3& impulse)
{
    if (bodies.BodyA != nullptr && bodies.InverseMassA > 0.0f)
    {
        WakeIfSleeping(bodies.BodyA);
        bodies.BodyA->LinearVelocity -= impulse * bodies.InverseMassA;
        bodies.BodyA->AngularVelocity -= bodies.InverseInertiaA
            * math::Vec3::Cross(point - bodies.TransformA->Position, impulse);
    }

    if (bodies.BodyB != nullptr && bodies.InverseMassB > 0.0f)
    {
        WakeIfSleeping(bodies.BodyB);
        bodies.BodyB->LinearVelocity += impulse * bodies.InverseMassB;
        bodies.BodyB->AngularVelocity += bodies.InverseInertiaB
            * math::Vec3::Cross(point - bodies.TransformB->Position, impulse);
    }
}

// J = -(v・n) / 有効質量 を求めるための分母を計算します。
float ComputeEffectiveMassDenominator(
    const SolverBodies& bodies,
    const math::Vec3& point,
    const math::Vec3& direction)
{
    const math::Vec3 ra = point - bodies.TransformA->Position;
    const math::Vec3 rb = point - bodies.TransformB->Position;
    const math::Vec3 angularA = math::Vec3::Cross(
        bodies.InverseInertiaA * math::Vec3::Cross(ra, direction),
        ra);
    const math::Vec3 angularB = math::Vec3::Cross(
        bodies.InverseInertiaB * math::Vec3::Cross(rb, direction),
        rb);

    return bodies.InverseMassSum + math::Vec3::Dot(direction, angularA + angularB);
}

void ApplyOrientationCorrection(
    TransformComponent* transform,
    RigidBodyComponent* body,
    const math::Vec3& delta)
{
    // 位置補正に伴う角度補正をQuaternion微小回転で適用します。
    if (transform == nullptr
        || body == nullptr
        || body->Type != BodyType::Dynamic
        || delta.LengthSq() <= 1.0e-16f)
    {
        return;
    }

    EnsurePhysicsOrientation(*transform, *body);
    const math::Quat deltaQuat{ delta.x, delta.y, delta.z, 0.0f };
    body->Orientation = (body->Orientation + (deltaQuat * body->Orientation) * 0.5f).Normalized();
    transform->Rotation = PhysicsEulerFromOrientation(body->Orientation);
}

// 貫通補正フェーズ: 位置誤差を減らす方向に位置と姿勢を直接補正します。
void SolvePositionConstraint(
    Scene& scene,
    ContactManifold& manifold,
    const ContactSolverSettings& settings)
{
    if (manifold.IsTrigger || manifold.PointCount == 0)
    {
        return;
    }

    SolverBodies bodies{};
    math::Vec3 normal{};
    if (GetSolverBodies(scene, manifold, bodies) == false || GetNormal(manifold, normal) == false)
    {
        return;
    }

    InitializePositionAnchors(bodies, manifold);

    const float percent = std::clamp(settings.PositionCorrectionPercent, 0.0f, 1.0f);
    const float slop = std::max(settings.PenetrationSlop, 0.0f);

    for (std::size_t i = 0; i < manifold.PointCount; ++i)
    {
        ContactPoint& point = manifold.Points[i];
        const math::Vec3 anchorA = WorldAnchorA(bodies, point);
        const math::Vec3 anchorB = WorldAnchorB(bodies, point);

        // 初期separationに現在アンカー差を足し、逐次更新で貫通量を追従させます。
        const float separation = point.InitialSeparation + math::Vec3::Dot(anchorB - anchorA, normal);
        const float penetration = std::max(-separation, 0.0f);
        point.Penetration = penetration;

        const float error = std::max(penetration - slop, 0.0f);
        if (error <= 0.0f)
        {
            continue;
        }

        const math::Vec3 contactPoint = (anchorA + anchorB) * 0.5f;
        const math::Vec3 ra = contactPoint - bodies.TransformA->Position;
        const math::Vec3 rb = contactPoint - bodies.TransformB->Position;

        // 反復中の姿勢変化を反映するため、ワールド慣性は毎点更新します。
        bodies.InverseInertiaA = ComputeWorldInverseInertia(
            bodies.TransformA,
            bodies.BodyA,
            bodies.ColliderA);
        bodies.InverseInertiaB = ComputeWorldInverseInertia(
            bodies.TransformB,
            bodies.BodyB,
            bodies.ColliderB);

        const float denominator = ComputeEffectiveMassDenominator(bodies, contactPoint, normal);
        if (denominator <= 1.0e-12f)
        {
            continue;
        }

        const float lambda = error * percent / denominator;

        if (bodies.BodyA != nullptr && bodies.InverseMassA > 0.0f)
        {
            bodies.TransformA->Position -= normal * (lambda * bodies.InverseMassA);
            ApplyOrientationCorrection(
                bodies.TransformA,
                bodies.BodyA,
                -(bodies.InverseInertiaA * math::Vec3::Cross(ra, normal)) * lambda);
        }

        if (bodies.BodyB != nullptr && bodies.InverseMassB > 0.0f)
        {
            bodies.TransformB->Position += normal * (lambda * bodies.InverseMassB);
            ApplyOrientationCorrection(
                bodies.TransformB,
                bodies.BodyB,
                (bodies.InverseInertiaB * math::Vec3::Cross(rb, normal)) * lambda);
        }
    }
}

// Warm Startフェーズ: 前フレームの蓄積インパルスを初期値として先に適用します。
void WarmStartConstraint(Scene& scene, ContactManifold& manifold)
{
    if (manifold.IsTrigger || manifold.PointCount == 0)
    {
        return;
    }

    SolverBodies bodies{};
    math::Vec3 normal{};
    if (GetSolverBodies(scene, manifold, bodies) == false || GetNormal(manifold, normal) == false)
    {
        return;
    }

    for (std::size_t i = 0; i < manifold.PointCount; ++i)
    {
        ContactPoint& point = manifold.Points[i];

        math::Vec3 impulse = normal * std::max(point.AccumulatedNormalImpulse, 0.0f);
        if (point.CachedTangent.LengthSq() > 1.0e-12f)
        {
            impulse += point.CachedTangent.Normalized() * point.AccumulatedTangentImpulse;
        }

        ApplyImpulseAtPoint(bodies, point.Position, impulse);
    }
}

// 速度制約の1接触点解法: 法線方向反発と接線方向摩擦を順に解きます。
void SolvePointVelocityConstraint(
    SolverBodies& bodies,
    ContactManifold& manifold,
    ContactPoint& point,
    const math::Vec3& normal,
    const ContactSolverSettings& settings)
{
    // 法線方向の相対速度を先に解いて貫入側速度を抑え、
    // その後に接線方向を解いて摩擦を適用します。
    math::Vec3 relativeVelocity =
        GetVelocityAtPoint(bodies.BodyB, bodies.TransformB, point.Position)
        - GetVelocityAtPoint(bodies.BodyA, bodies.TransformA, point.Position);

    const float normalVelocity = math::Vec3::Dot(relativeVelocity, normal);

    float restitution = 0.0f;
    if (normalVelocity < -std::max(settings.RestitutionVelocityThreshold, 0.0f))
    {
        restitution = std::clamp(manifold.Restitution, 0.0f, 1.0f);
    }

    const float normalDenominator = ComputeEffectiveMassDenominator(bodies, point.Position, normal);
    if (normalDenominator <= 1.0e-12f)
    {
        return;
    }

    const float deltaImpulse = -(1.0f + restitution) * normalVelocity / normalDenominator;
    const float oldNormalImpulse = point.AccumulatedNormalImpulse;
    point.AccumulatedNormalImpulse = std::max(oldNormalImpulse + deltaImpulse, 0.0f);

    ApplyImpulseAtPoint(
        bodies,
        point.Position,
        normal * (point.AccumulatedNormalImpulse - oldNormalImpulse));

    relativeVelocity =
        GetVelocityAtPoint(bodies.BodyB, bodies.TransformB, point.Position)
        - GetVelocityAtPoint(bodies.BodyA, bodies.TransformA, point.Position);

    math::Vec3 tangent = relativeVelocity - normal * math::Vec3::Dot(relativeVelocity, normal);
    if (tangent.LengthSq() <= 1.0e-12f)
    {
        return;
    }

    tangent = tangent.Normalized();
    point.CachedTangent = tangent;

    const float tangentDenominator = ComputeEffectiveMassDenominator(bodies, point.Position, tangent);
    if (tangentDenominator <= 1.0e-12f)
    {
        return;
    }

    const float deltaTangentImpulse = -math::Vec3::Dot(relativeVelocity, tangent) / tangentDenominator;
    const float oldTangentImpulse = point.AccumulatedTangentImpulse;
    const float candidateImpulse = oldTangentImpulse + deltaTangentImpulse;
    const float staticLimit = point.AccumulatedNormalImpulse * std::max(manifold.StaticFriction, 0.0f);

    float newTangentImpulse = 0.0f;
    if (std::abs(candidateImpulse) <= staticLimit)
    {
        newTangentImpulse = candidateImpulse;
    }
    else
    {
        const float dynamicLimit = point.AccumulatedNormalImpulse * std::max(manifold.DynamicFriction, 0.0f);
        newTangentImpulse = std::clamp(candidateImpulse, -dynamicLimit, dynamicLimit);
    }

    point.AccumulatedTangentImpulse = newTangentImpulse;
    ApplyImpulseAtPoint(
        bodies,
        point.Position,
        tangent * (newTangentImpulse - oldTangentImpulse));
}

// 速度制約フェーズ: 1マニホールド内の全接触点を反復解法で更新します。
void SolveVelocityConstraint(
    Scene& scene,
    ContactManifold& manifold,
    const ContactSolverSettings& settings)
{
    if (manifold.IsTrigger || manifold.PointCount == 0)
    {
        return;
    }

    SolverBodies bodies{};
    math::Vec3 normal{};
    if (GetSolverBodies(scene, manifold, bodies) == false || GetNormal(manifold, normal) == false)
    {
        return;
    }

    for (std::size_t i = 0; i < manifold.PointCount; ++i)
    {
        SolvePointVelocityConstraint(bodies, manifold, manifold.Points[i], normal, settings);
    }
}

} // namespace

// ソルバー全体: Warm Start -> Velocity Iteration -> Position Iteration の順で実行します。
void SolveContactManifolds(
    Scene& scene,
    std::vector<ContactManifold>& manifolds,
    float dt,
    const ContactSolverSettings& settings)
{
    if (dt <= 0.0f)
    {
        return;
    }

    {
        RAVEN_PROFILE_SCOPE("Physics.ContactSolver.WarmStart");
        if (settings.EnableWarmStart)
        {
            for (ContactManifold& manifold : manifolds)
            {
                WarmStartConstraint(scene, manifold);
            }
        }
    }
    {
        RAVEN_PROFILE_SCOPE("Physics.ContactSolver.VelocityIterations");
        const uint32_t velocityIterations = std::max(settings.VelocityIterations, 1u);
        // Gauss-Seidel反復: 新しい速度を即時反映しながら順次収束させます。
        for (uint32_t iteration = 0; iteration < velocityIterations; ++iteration)
        {
            for (ContactManifold& manifold : manifolds)
            {
                SolveVelocityConstraint(scene, manifold, settings);
            }
        }
    }
    {
        RAVEN_PROFILE_SCOPE("Physics.ContactSolver.PositionIterations");
        const uint32_t positionIterations = std::max(settings.PositionIterations, 1u);
        // 位置補正は速度解法後に実行し、残留貫通を段階的に押し戻します。
        for (uint32_t iteration = 0; iteration < positionIterations; ++iteration)
        {
            for (ContactManifold& manifold : manifolds)
            {
                SolvePositionConstraint(scene, manifold, settings);
            }
        }
    }
}

void SolveContactManifold(Scene& scene, ContactManifold& manifold, float dt)
{
    std::vector<ContactManifold> one{ manifold };
    SolveContactManifolds(scene, one, dt);
    manifold = one.front();
}

} // namespace ph
} // namespace Raven
