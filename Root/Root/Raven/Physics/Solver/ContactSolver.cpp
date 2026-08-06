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

// RigidBodyを持たないColliderはStatic Bodyとして扱います。
// 例えば床PlaneはTransform + Colliderだけでも利用でき、この場合の逆質量は0です。
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
    if (rigidBody == nullptr)
    {
        return math::Vec3{};
    }

    return rigidBody->LinearVelocity;
}

void WakeIfDynamic(RigidBodyComponent* rigidBody)
{
    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic)
    {
        return;
    }

    rigidBody->IsSleeping = false;
    rigidBody->SleepTimer = 0.0f;
}

} // namespace

void SolveContact(Scene& scene, const Contact& contact, float dt)
{
    // Triggerは接触イベントを発生させるための領域です。
    // 衝突情報は生成しますが、位置や速度を変更して物体を押し返してはいけません。
    if (contact.IsTrigger)
    {
        return;
    }

    if (!scene.IsEntityAlive(contact.A) || !scene.IsEntityAlive(contact.B))
    {
        return;
    }

    TransformComponent* transformA =
        scene.TryGetComponent<TransformComponent>(contact.A.GetIndex());
    TransformComponent* transformB =
        scene.TryGetComponent<TransformComponent>(contact.B.GetIndex());

    if (transformA == nullptr || transformB == nullptr)
    {
        return;
    }

    RigidBodyComponent* rigidBodyA =
        scene.TryGetComponent<RigidBodyComponent>(contact.A.GetIndex());
    RigidBodyComponent* rigidBodyB =
        scene.TryGetComponent<RigidBodyComponent>(contact.B.GetIndex());

    const float inverseMassA = GetInverseMass(rigidBodyA);
    const float inverseMassB = GetInverseMass(rigidBodyB);
    const float inverseMassSum = inverseMassA + inverseMassB;

    // 両方がStaticの場合は、位置も速度も変更できません。
    if (inverseMassSum <= 0.0f)
    {
        return;
    }

    // CollisionDetection側で法線を正規化していますが、Solver単体の安全性を
    // 保つため、ここでも長さを確認します。
    const float normalLengthSquared = contact.Normal.LengthSq();
    constexpr float NormalEpsilonSquared = 1.0e-12f;

    if (normalLengthSquared <= NormalEpsilonSquared)
    {
        return;
    }

    const math::Vec3 normal =
        contact.Normal / std::sqrt(normalLengthSquared);

    // ========================================================================
    // 1. 位置補正
    // ========================================================================
    // Impulseは速度を変更しますが、既に発生している貫通そのものは解消しません。
    // そこで、逆質量の割合に応じてAとBを法線の反対方向へ分離します。
    //
    // Contact::NormalはAからBへ向くため、
    //   Aは -Normal 方向
    //   Bは +Normal 方向
    // へ移動させます。
    //
    // Slopは微小な貫通を許容する値です。浮動小数点誤差によって毎Step細かく
    // 押し戻し続ける振動を軽減します。
    constexpr float PenetrationSlop = 0.001f;
    constexpr float CorrectionPercent = 0.8f;

    const float penetration = std::max(contact.Penetration, 0.0f);
    const float correctionMagnitude =
        std::max(penetration - PenetrationSlop, 0.0f)
        * CorrectionPercent
        / inverseMassSum;

    const math::Vec3 positionCorrection = normal * correctionMagnitude;

    transformA->Position -= positionCorrection * inverseMassA;
    transformB->Position += positionCorrection * inverseMassB;

    // ========================================================================
    // 2. 法線方向の反発Impulse
    // ========================================================================
    // 相対速度はBから見たAではなく、Contact法線の規約に合わせて
    //
    //     relativeVelocity = velocityB - velocityA
    //
    // とします。
    // AがBへ近づいている場合、dot(relativeVelocity, normal)は負になります。
    math::Vec3 velocityA = GetLinearVelocity(rigidBodyA);
    math::Vec3 velocityB = GetLinearVelocity(rigidBodyB);
    math::Vec3 relativeVelocity = velocityB - velocityA;

    const float velocityAlongNormal =
        math::Vec3::Dot(relativeVelocity, normal);

    // 正の値なら既に離れる方向へ動いています。
    // この状態で反発Impulseを加えると、離れている物体をさらに加速してしまいます。
    if (velocityAlongNormal > 0.0f)
    {
        return;
    }

    const float restitution =
        std::clamp(contact.Restitution, 0.0f, 1.0f);

    const float normalImpulseMagnitude =
        -(1.0f + restitution)
        * velocityAlongNormal
        / inverseMassSum;

    const math::Vec3 normalImpulse =
        normal * normalImpulseMagnitude;

    if (rigidBodyA != nullptr && inverseMassA > 0.0f)
    {
        rigidBodyA->LinearVelocity -= normalImpulse * inverseMassA;
        WakeIfDynamic(rigidBodyA);
    }

    if (rigidBodyB != nullptr && inverseMassB > 0.0f)
    {
        rigidBodyB->LinearVelocity += normalImpulse * inverseMassB;
        WakeIfDynamic(rigidBodyB);
    }

    // ========================================================================
    // 3. 接線方向の摩擦Impulse
    // ========================================================================
    // 法線Impulse適用後の速度で再計算します。
    velocityA = GetLinearVelocity(rigidBodyA);
    velocityB = GetLinearVelocity(rigidBodyB);
    relativeVelocity = velocityB - velocityA;

    // 相対速度から法線成分を除くと、接触面に沿った滑り速度になります。
    math::Vec3 tangent =
        relativeVelocity
        - normal * math::Vec3::Dot(relativeVelocity, normal);

    const float tangentLengthSquared = tangent.LengthSq();
    constexpr float TangentEpsilonSquared = 1.0e-12f;

    if (tangentLengthSquared <= TangentEpsilonSquared)
    {
        return;
    }

    tangent /= std::sqrt(tangentLengthSquared);

    float tangentImpulseMagnitude =
        -math::Vec3::Dot(relativeVelocity, tangent)
        / inverseMassSum;

    const float staticFriction =
        std::max(contact.StaticFriction, 0.0f);
    const float dynamicFriction =
        std::max(contact.DynamicFriction, 0.0f);

    math::Vec3 frictionImpulse{};

    // Coulomb摩擦モデル
    // 静止摩擦の限界以内なら、接線速度を打ち消すImpulseをそのまま適用します。
    // 限界を超えて滑っている場合は、動摩擦係数で大きさを制限します。
    if (std::abs(tangentImpulseMagnitude)
        <= normalImpulseMagnitude * staticFriction)
    {
        frictionImpulse = tangent * tangentImpulseMagnitude;
    }
    else
    {
        frictionImpulse =
            tangent
            * (-normalImpulseMagnitude * dynamicFriction);
    }

    if (rigidBodyA != nullptr && inverseMassA > 0.0f)
    {
        rigidBodyA->LinearVelocity -= frictionImpulse * inverseMassA;
    }

    if (rigidBodyB != nullptr && inverseMassB > 0.0f)
    {
        rigidBodyB->LinearVelocity += frictionImpulse * inverseMassB;
    }

    // dtは将来Baumgarte Stabilizationや反復Solverで使用します。
    // 現在の最小Solverでは位置補正を直接行うため未使用です。
    static_cast<void>(dt);
}

} // namespace ph

} // namespace Raven
