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
    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic)
    {
        return 0.0f;
    }
    return std::max(rigidBody->InverseMass, 0.0f);
}

math::Vec3 GetLinearVelocity(const RigidBodyComponent* rigidBody)
{
    return rigidBody != nullptr ? rigidBody->LinearVelocity : math::Vec3{};
}

void WakeIfSleeping(RigidBodyComponent* rigidBody)
{
    if (rigidBody != nullptr
        && rigidBody->Type == BodyType::Dynamic
        && rigidBody->IsSleeping)
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
    if (!scene.IsEntityAlive(manifold.A) || !scene.IsEntityAlive(manifold.B))
    {
        return false;
    }

    out.TransformA = scene.TryGetComponent<TransformComponent>(manifold.A.GetIndex());
    out.TransformB = scene.TryGetComponent<TransformComponent>(manifold.B.GetIndex());
    if (out.TransformA == nullptr || out.TransformB == nullptr)
    {
        return false;
    }

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
    if (lengthSquared <= 1.0e-12f)
    {
        return false;
    }
    outNormal = manifold.Normal / std::sqrt(lengthSquared);
    return true;
}

float GetMaximumPenetration(const ContactManifold& manifold)
{
    float penetration = 0.0f;
    const std::size_t pointCount =
        std::min(manifold.PointCount, ContactManifold::MaxContactPointCount);

    // Rotationを解かない現段階では、同一Manifoldの全点は同じ並進自由度を
    // 拘束します。平均や合計ではなく最大貫通量を使い、面全体を1回だけ補正します。
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        penetration = std::max(penetration, std::max(manifold.Points[i].Penetration, 0.0f));
    }
    return penetration;
}

void ApplyPositionCorrection(
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
    if (!GetSolverBodies(scene, manifold, bodies) || !GetNormal(manifold, normal))
    {
        return;
    }

    const float penetration = GetMaximumPenetration(manifold);
    const float correctionMagnitude =
        std::max(penetration - std::max(settings.PenetrationSlop, 0.0f), 0.0f)
        * std::clamp(settings.PositionCorrectionPercent, 0.0f, 1.0f)
        / bodies.InverseMassSum;

    const math::Vec3 correction = normal * correctionMagnitude;
    bodies.TransformA->Position -= correction * bodies.InverseMassA;
    bodies.TransformB->Position += correction * bodies.InverseMassB;
}

void ApplyImpulse(
    RigidBodyComponent* bodyA,
    RigidBodyComponent* bodyB,
    float inverseMassA,
    float inverseMassB,
    const math::Vec3& impulse)
{
    if (bodyA != nullptr && inverseMassA > 0.0f)
    {
        WakeIfSleeping(bodyA);
        bodyA->LinearVelocity -= impulse * inverseMassA;
    }
    if (bodyB != nullptr && inverseMassB > 0.0f)
    {
        WakeIfSleeping(bodyB);
        bodyB->LinearVelocity += impulse * inverseMassB;
    }
}

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
    if (!GetSolverBodies(scene, manifold, bodies) || !GetNormal(manifold, normal))
    {
        return;
    }

    // ------------------------------------------------------------------------
    // 現段階ではManifold代表ImpulseをPoint[0]へ保存します。
    // ------------------------------------------------------------------------
    // Box-Boxの4点は接触面を表しますが、Angular Impulseが無い間は全点が同じ
    // linear velocity constraintです。4点を独立に解くと同じ制約を4回重複して
    // 適用するため、Point[0]を代表Constraintとして使用します。
    ContactPoint& constraint = manifold.Points[0];

    math::Vec3 relativeVelocity =
        GetLinearVelocity(bodies.BodyB) - GetLinearVelocity(bodies.BodyA);
    const float velocityAlongNormal = math::Vec3::Dot(relativeVelocity, normal);

    // 静止に近い接触でrestitutionを掛けると、床上のBodyが細かく跳ね続けます。
    // 一定以上の衝突速度にだけ反発係数を適用します。
    float restitution = 0.0f;
    if (velocityAlongNormal < -std::max(settings.RestitutionVelocityThreshold, 0.0f))
    {
        restitution = std::clamp(manifold.Restitution, 0.0f, 1.0f);
    }

    const float deltaNormalImpulse =
        -(1.0f + restitution) * velocityAlongNormal / bodies.InverseMassSum;

    // Sequential Impulseでは「今回必要なImpulse」をそのまま適用するのではなく、
    // 累積Impulseを更新して制約条件 lambda >= 0 を満たします。
    // これにより後続Manifoldの影響で物体が離れ始めた場合、以前加えたImpulseを
    // 次のiterationで部分的に戻すことができます。
    const float oldNormalImpulse = constraint.AccumulatedNormalImpulse;
    constraint.AccumulatedNormalImpulse =
        std::max(oldNormalImpulse + deltaNormalImpulse, 0.0f);
    const float appliedNormalImpulse =
        constraint.AccumulatedNormalImpulse - oldNormalImpulse;

    ApplyImpulse(
        bodies.BodyA,
        bodies.BodyB,
        bodies.InverseMassA,
        bodies.InverseMassB,
        normal * appliedNormalImpulse);

    // ------------------------------------------------------------------------
    // 摩擦Constraint
    // ------------------------------------------------------------------------
    relativeVelocity =
        GetLinearVelocity(bodies.BodyB) - GetLinearVelocity(bodies.BodyA);

    math::Vec3 tangent =
        relativeVelocity - normal * math::Vec3::Dot(relativeVelocity, normal);
    const float tangentLengthSquared = tangent.LengthSq();
    if (tangentLengthSquared <= 1.0e-12f)
    {
        return;
    }
    tangent /= std::sqrt(tangentLengthSquared);

    const float deltaTangentImpulse =
        -math::Vec3::Dot(relativeVelocity, tangent) / bodies.InverseMassSum;

    // Coulomb摩擦円錐を1次元接線Constraintとして近似します。
    // 累積摩擦Impulseは現在の累積Normal Impulseを基準にClampします。
    const float staticFriction = std::max(manifold.StaticFriction, 0.0f);
    const float dynamicFriction = std::max(manifold.DynamicFriction, 0.0f);
    const float oldTangentImpulse = constraint.AccumulatedTangentImpulse;
    const float candidateTangentImpulse = oldTangentImpulse + deltaTangentImpulse;
    const float staticLimit = constraint.AccumulatedNormalImpulse * staticFriction;

    float newTangentImpulse = 0.0f;
    if (std::abs(candidateTangentImpulse) <= staticLimit)
    {
        newTangentImpulse = candidateTangentImpulse;
    }
    else
    {
        const float dynamicLimit = constraint.AccumulatedNormalImpulse * dynamicFriction;
        newTangentImpulse = std::clamp(candidateTangentImpulse, -dynamicLimit, dynamicLimit);
    }

    constraint.AccumulatedTangentImpulse = newTangentImpulse;
    const float appliedTangentImpulse = newTangentImpulse - oldTangentImpulse;

    ApplyImpulse(
        bodies.BodyA,
        bodies.BodyB,
        bodies.InverseMassA,
        bodies.InverseMassB,
        tangent * appliedTangentImpulse);
}

void ResetAccumulatedImpulses(ContactManifold& manifold)
{
    const std::size_t pointCount =
        std::min(manifold.PointCount, ContactManifold::MaxContactPointCount);
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        manifold.Points[i].AccumulatedNormalImpulse = 0.0f;
        manifold.Points[i].AccumulatedTangentImpulse = 0.0f;
    }
}

} // namespace

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

    // Contact Persistence/Warm Startは次段階なので、現時点では毎Stepゼロから解きます。
    // ただし「同一Step内のiteration間」ではAccumulated Impulseを維持します。
    for (ContactManifold& manifold : manifolds)
    {
        ResetAccumulatedImpulses(manifold);
        ApplyPositionCorrection(scene, manifold, settings);
    }

    const uint32_t iterationCount = std::max(settings.VelocityIterations, 1u);
    for (uint32_t iteration = 0; iteration < iterationCount; ++iteration)
    {
        // 全Manifoldを1回ずつ解いてから次iterationへ進むGauss-Seidel方式です。
        // 積み重なったBodyでは下側接触のImpulseが上側へ反復的に伝播します。
        for (ContactManifold& manifold : manifolds)
        {
            SolveVelocityConstraint(scene, manifold, settings);
        }
    }
}

void SolveContactManifold(
    Scene& scene,
    ContactManifold& manifold,
    float dt)
{
    // 既存呼び出し互換用。単一Manifoldでも同じ反復Solverを使用します。
    std::vector<ContactManifold> singleManifold;
    singleManifold.push_back(manifold);
    SolveContactManifolds(scene, singleManifold, dt);
    manifold = singleManifold.front();
}

} // namespace ph
} // namespace Raven
