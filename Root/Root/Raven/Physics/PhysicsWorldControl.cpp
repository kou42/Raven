#include "Raven/Physics/PhysicsWorld.h"

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

namespace ph
{

namespace
{

// ============================================================================
// TryGetAliveRigidBody
// ============================================================================
// PhysicsWorldの公開操作APIでは、毎回次の安全確認が必要になります。
//
//   1. Entityが現在も生存しているか
//   2. 古いGenerationのEntity Handleではないか
//   3. RigidBodyComponentを持っているか
//
// この確認を各APIへ個別に記述すると、将来APIが増えた際に確認漏れが起こりやすく
// なります。そのため、共通の補助関数へまとめています。
RigidBodyComponent* TryGetAliveRigidBody(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity))
    {
        return nullptr;
    }

    return scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
}

const RigidBodyComponent* TryGetAliveRigidBody(const Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity))
    {
        return nullptr;
    }

    return scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
}

TransformComponent* TryGetAliveTransform(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity))
    {
        return nullptr;
    }

    return scene.TryGetComponent<TransformComponent>(entity.GetIndex());
}

// Dynamic Bodyへ外部から運動を与えた場合は、Sleepを解除する必要があります。
// SleepTimerもリセットしないと、Wake直後の次Stepで再びSleepへ戻る可能性があります。
void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}

} // namespace

void PhysicsWorld::AddTorque(Scene& scene, Entity entity, const math::Vec3& torque)
{
    RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    if (rigidBody == nullptr)
    {
        return;
    }

    // Static Bodyは無限慣性として扱い、Torqueでは回転しません。
    // Kinematic Bodyの回転はゲームロジック側が決めるため、外部Torqueを蓄積しません。
    if (rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);

    // TorqueもForceと同様に、同じStep中に複数のSystemから追加できるよう加算します。
    // 第4段階では、この蓄積値へ逆慣性テンソルを掛けて角加速度を求めます。
    rigidBody->Torque += torque;
}

void PhysicsWorld::SetLinearVelocity(
    Scene& scene,
    Entity entity,
    const math::Vec3& velocity
)
{
    RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    if (rigidBody == nullptr)
    {
        return;
    }

    // Static Bodyは物理ワールド内で移動しない契約です。
    // Staticへ速度を設定できてしまうと、Transform更新とBroad Phaseの前提が崩れます。
    if (rigidBody->Type == BodyType::Static)
    {
        return;
    }

    rigidBody->LinearVelocity = velocity;

    // Dynamic Bodyへ速度を設定した場合は、必ずSleepを解除します。
    // Kinematic BodyはSleep判定対象外なのでWake処理は不要です。
    if (rigidBody->Type == BodyType::Dynamic)
    {
        WakeRigidBody(*rigidBody);
    }
}

math::Vec3 PhysicsWorld::GetLinearVelocity(const Scene& scene, Entity entity) const
{
    const RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    if (rigidBody == nullptr)
    {
        // 取得失敗を例外にすると、デバッグ表示など読み取り頻度の高い処理で
        // 呼び出し側の分岐が増えます。第1段階では安全な既定値として0を返します。
        return math::Vec3{};
    }

    return rigidBody->LinearVelocity;
}

void PhysicsWorld::Teleport(
    Scene& scene,
    Entity entity,
    const math::Vec3& position
)
{
    TransformComponent* transform = TryGetAliveTransform(scene, entity);
    if (transform == nullptr)
    {
        return;
    }

    transform->Position = position;

    // Teleportは位置だけを瞬間的に変更し、速度は維持します。
    // 例えば落下中のBodyを別の高さへ移動した場合、落下速度をそのまま継続できます。
    // 速度も止めたい場合は、呼び出し側でSetLinearVelocity(entity, Vec3{})を併用します。
    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody != nullptr && rigidBody->Type == BodyType::Dynamic)
    {
        WakeRigidBody(*rigidBody);
    }

    // Broad Phase導入後は、ここで古いAABBを破棄して新しい位置のAABBを再登録します。
    // Transformを外部から直接書き換えるのではなくTeleport APIを通す主な理由です。
}

void PhysicsWorld::MovePosition(
    Scene& scene,
    Entity entity,
    const math::Vec3& position
)
{
    TransformComponent* transform = TryGetAliveTransform(scene, entity);
    RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);

    if (transform == nullptr || rigidBody == nullptr)
    {
        return;
    }

    // MovePositionはKinematic Body専用とします。
    // Dynamic Bodyを直接移動させると、力・速度・衝突応答との整合性が崩れるためです。
    // Dynamic Bodyを瞬間移動させる場合はTeleportを使用してください。
    if (rigidBody->Type != BodyType::Kinematic)
    {
        return;
    }

    transform->Position = position;

    // 現段階では位置を直接設定します。
    // 衝突処理導入後は、前回位置との差をfixedDeltaTimeで割ってKinematic速度を求め、
    // 接触したDynamic Bodyへ「動く床の速度」として伝達する設計へ拡張します。
}

} // namespace ph

} // namespace Raven
