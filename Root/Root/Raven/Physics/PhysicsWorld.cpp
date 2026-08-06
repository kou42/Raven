#include <algorithm>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include "Raven/Physics/PhysicsWorld.h"

namespace Raven
{

namespace ph // 物理演算のための名前空間
{

namespace
{

// ============================================================================
// WakeRigidBody
// ============================================================================
// Sleep中のBodyへ外力や力積が加わった場合、そのままSleep状態を維持すると
// 次回のPhysicsWorld::Stepで更新対象から除外され、入力した力が反映されません。
//
// そのため、Bodyへ運動を発生させる操作を行う直前には必ずWakeさせます。
// この処理を小さな共通関数へまとめることで、AddForce、AddImpulse、WakeUpの
// 状態変更を同じ規則に揃えています。
void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}

} // namespace

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
    // ========================================================================
    // 力から速度を更新する処理
    // ========================================================================
    // ニュートンの運動方程式
    //
    //     F = m * a
    //
    // を加速度について解くと、
    //
    //     a = F / m = F * InverseMass
    //
    // になります。
    // さらに、一定時間dtの間に速度がどれだけ変化するかは
    //
    //     deltaVelocity = acceleration * dt
    //
    // です。
    //
    // 現在の積分方法はSemi-Implicit Euler法です。
    // 先に速度を更新し、その更新済み速度を使って位置を更新します。
    // 単純なExplicit Euler法よりも、重力や衝突を扱うゲーム物理で
    // エネルギーが発散しにくく、実装も簡潔という利点があります。
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        // Static Bodyは無限質量として扱うため、外力では動きません。
        // Kinematic Bodyもゲームロジックが移動を決めるため、外力を適用しません。
        if (rigidBody.Type != BodyType::Dynamic)
        {
            continue;
        }

        // Sleep中のBodyは、明示的にWakeされるまで積分しません。
        // AddForceやAddImpulseはBodyをWakeしてから値を反映します。
        if (rigidBody.IsSleeping)
        {
            continue;
        }

        // InverseMassが0なら、設定上はDynamicでも物理的には動かせません。
        // 0除算を避けるだけでなく、不正な質量設定から状態を保護します。
        if (rigidBody.InverseMass <= 0.0f)
        {
            continue;
        }

        // 重力は質量によらず同じ加速度として作用します。
        // Forceへ m*g を一度加えた後にInverseMassを掛けても結果はgですが、
        // ここでは重力加速度を直接accelerationへ加えることで、巨大な質量値による
        // 不要な数値拡大を避けています。
        math::Vec3 acceleration{};

        if (rigidBody.UseGravity)
        {
            acceleration += m_Gravity;
        }

        // ゲーム側から蓄積された外力を加速度へ変換します。
        acceleration += rigidBody.Force * rigidBody.InverseMass;

        // Semi-Implicit Euler法の速度更新部分です。
        rigidBody.LinearVelocity += acceleration * dt;
    }
}

void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    // ========================================================================
    // 線形速度へDampingを適用する処理
    // ========================================================================
    // 現実の空気抵抗を厳密に再現するものではなく、ゲーム中で微小な速度が
    // 永久に残り続けることを防ぐための数値的な減衰です。
    //
    // velocity *= 0.99f のようにフレームごとの固定倍率を使うと、
    // 30Hz、60Hz、120Hzで1秒後の速度が変わります。
    // ここではdtを含む
    //
    //     dampingFactor = 1 / (1 + damping * dt)
    //
    // を利用し、固定更新間隔を変更した場合にも挙動が大きく変化しにくい形にします。
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping)
        {
            continue;
        }

        // 負のDampingは速度を増幅してしまうため、0以上へ制限します。
        const float linearDamping = std::max(rigidBody.LinearDamping, 0.0f);
        const float dampingFactor = 1.0f / (1.0f + linearDamping * dt);

        rigidBody.LinearVelocity *= dampingFactor;

        // AngularVelocityのDampingは回転運動を実装する第4段階で追加します。
        // データだけは既にRigidBodyComponentへ用意してありますが、現段階では
        // Transform::Rotationへ反映しません。
    }
}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    // 第1段階では衝突検出をまだ行いません。
    // 第2段階でSphereとPlaneからContactを生成する処理を追加します。
}

void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    // 第1段階では衝突応答をまだ行いません。
    // 第2段階で位置補正、反発Impulse、摩擦Impulseを追加します。
}

void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    // ========================================================================
    // 速度から位置を更新する処理
    // ========================================================================
    // 速度の定義
    //
    //     velocity = displacement / time
    //
    // から、dt秒間の移動量は
    //
    //     displacement = velocity * dt
    //
    // となります。
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        // Static Bodyは移動しません。
        if (rigidBody.Type == BodyType::Static)
        {
            continue;
        }

        // Dynamic BodyがSleep中なら位置更新を省略します。
        // Kinematic Bodyはゲーム側が設定したLinearVelocityで移動できるよう、
        // IsSleepingの値に関係なく更新対象にします。
        if (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping)
        {
            continue;
        }

        transform.Position += rigidBody.LinearVelocity * dt;
    }
}

void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    // ========================================================================
    // 簡易Sleep判定
    // ========================================================================
    // ほぼ停止したBodyを毎Step更新し続けると、Body数が多いSceneでは無駄な処理が
    // 増えます。一定時間ほぼ停止していたBodyをSleep状態へ移し、外力・力積などが
    // 加わるまで積分対象から外します。
    //
    // 現段階では線形速度だけを判定します。
    // 回転運動導入後はAngularVelocityと接触状態も考慮し、床の上で安定しているBody
    // だけがSleepへ入る、より厳密な判定へ拡張します。
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        if (rigidBody.Type != BodyType::Dynamic)
        {
            continue;
        }

        if (!rigidBody.AllowSleep)
        {
            // Sleepを無効にしたBodyは常に起きた状態を維持します。
            WakeRigidBody(rigidBody);
            continue;
        }

        if (rigidBody.IsSleeping)
        {
            // Sleep中は速度を完全に0へ固定します。
            // 浮動小数点のごく小さな残りが、Wake後に不意な移動を起こすのを防ぎます。
            rigidBody.LinearVelocity = math::Vec3{};
            continue;
        }

        const float threshold = std::max(rigidBody.SleepThreshold, 0.0f);
        const float thresholdSquared = threshold * threshold;

        // Length()は平方根を計算しますが、大小比較だけならLengthSq()で十分です。
        // Body数が増えた際の余分な平方根計算を避けられます。
        if (rigidBody.LinearVelocity.LengthSq() <= thresholdSquared)
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
        else
        {
            // 再び動き始めた場合は連続静止時間をリセットします。
            rigidBody.SleepTimer = 0.0f;
        }
    }
}

void PhysicsWorld::ClearForces(Scene& scene)
{
    // ========================================================================
    // Step内で蓄積したForce/Torqueのクリア
    // ========================================================================
    // Forceは「次の1回の物理Stepで作用する入力」です。
    // クリアしないと、ゲーム側がAddForceを1度だけ呼んだ場合でも、その力が
    // 永久に加わり続けて速度が増加し続けます。
    //
    // 重力はm_Gravityから毎Step計算するため、Forceへ保持する必要はありません。
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        rigidBody.Force = math::Vec3{};
        rigidBody.Torque = math::Vec3{};
    }
}

void PhysicsWorld::AddForce(Scene& scene, Entity entity, const math::Vec3& force)
{
    // Entityが既に破棄されている場合や、別Generationの古いHandleである場合は
    // 何も行いません。これにより解放済みEntityへのアクセスを防ぎます。
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr)
    {
        return;
    }

    // StaticとKinematicは外力による運動の対象ではありません。
    if (rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);

    // 複数のSystemから加えられた力を合計できるよう、代入ではなく加算します。
    // 例: 重力以外に、風 + エンジン推力 + ばね力を同時に作用させられます。
    rigidBody->Force += force;
}

void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr)
    {
        return;
    }

    if (rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);

    // ========================================================================
    // ForceとImpulseの違い
    // ========================================================================
    // Forceは時間を通して作用し、ApplyForces内で dt を掛けて速度へ変換します。
    // 一方Impulseは、非常に短い時間に作用した力の積分値であり、速度を即座に
    //
    //     deltaVelocity = impulse * InverseMass
    //
    // だけ変化させます。
    // ジャンプ、爆発、弾丸、衝突応答など瞬間的な運動変化に適しています。
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
}

void PhysicsWorld::WakeUp(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
}

void PhysicsWorld::Step(Scene& scene, float dt)
{
    // 0以下のdtでは積分できません。
    // 呼び出し側の不正値によってDamping係数やSleepTimerが壊れることを防ぎます。
    if (dt <= 0.0f)
    {
        return;
    }

    // ========================================================================
    // 1回の固定物理Step
    // ========================================================================
    // 現在と将来の推奨順序は次の通りです。
    //
    //   1. 重力・外力から速度を更新
    //   2. 速度へDampingを適用
    //   3. Broad Phase / Narrow PhaseでContactを生成
    //   4. Contact ConstraintをImpulseで解決
    //   5. 更新済み速度から位置を更新
    //   6. Sleep判定
    //   7. 一時的なForce/Torqueをクリア
    //
    // 第1段階では3と4は空ですが、呼び出し位置を先に固定しておくことで、
    // 第2段階以降にPhysicsWorld全体の順序を作り直さずに拡張できます。
    ApplyForces(scene, dt);
    IntegrateVelocities(scene, dt);

    DetectCollisions(scene);
    SolveCollisions(scene, dt);

    IntegratePositions(scene, dt);
    UpdateSleeping(scene, dt);
    ClearForces(scene);
}

} // namespace ph

} // namespace Raven
