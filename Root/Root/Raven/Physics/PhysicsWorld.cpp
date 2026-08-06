#include <algorithm>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Solver/ContactSolver.h"

namespace Raven
{

namespace ph // 物理演算のための名前空間
{

namespace
{

// Sleep中のDynamic Bodyへ外力やImpulseを与える場合、次のStepから再び積分対象へ
// 戻す必要があります。SleepTimerも同時に0へ戻し、直後の再Sleepを防ぎます。
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
    // Force -> Acceleration -> Velocity
    // ========================================================================
    // ニュートンの第二法則 F = m*a より、加速度は
    //
    //     a = F / m = F * InverseMass
    //
    // です。加速度をdt秒積分して速度へ反映します。
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

        // 重力は質量に関係なく同じ加速度として作用するため、Forceへm*gを追加せず
        // 直接accelerationへ加算します。
        if (rigidBody.UseGravity)
        {
            acceleration += m_Gravity;
        }

        acceleration += rigidBody.Force * rigidBody.InverseMass;

        // Semi-Implicit Euler法の速度更新部分です。
        rigidBody.LinearVelocity += acceleration * dt;
    }
}

void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    // ========================================================================
    // 線形速度のDamping
    // ========================================================================
    // フレームごとの固定倍率ではなくdtを含む係数を使うことで、固定更新周波数を
    // 変更しても1秒間の減衰量が大きく変わりにくい形にします。
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping)
        {
            continue;
        }

        const float linearDamping = std::max(rigidBody.LinearDamping, 0.0f);
        const float dampingFactor = 1.0f / (1.0f + linearDamping * dt);

        rigidBody.LinearVelocity *= dampingFactor;
    }
}

void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    // ========================================================================
    // Velocity -> Position
    // ========================================================================
    // この位置更新を衝突判定より前に行うことが重要です。
    // 離散衝突判定では「このStepで移動した後の位置」を検査し、発生した貫通を
    // ContactSolverで押し戻します。
    for (auto [entity, transform, rigidBody]
        : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type == BodyType::Static)
        {
            continue;
        }

        if (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping)
        {
            continue;
        }

        transform.Position += rigidBody.LinearVelocity * dt;
    }
}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    // ========================================================================
    // Narrow Phase: Sphere vs Plane
    // ========================================================================
    // Contactは1 Physics Step限りの情報なので、毎回最初に破棄して再生成します。
    m_Contacts.clear();

    // 現段階ではBroad Phaseがないため、Collider同士を全探索します。
    // Sphere側とPlane側を分けて列挙することで、Sphere-Planeだけを対象にします。
    // 計算量はO(SphereCount * PlaneCount)です。
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
            if (planeCollider.Type != ColliderType::Plane)
            {
                continue;
            }

            // 同じEntityへSphereとPlaneを同時に設定するケースは通常ありませんが、
            // 自己接触を生成しないよう明示的に除外します。
            if (sphereEntity == planeEntity)
            {
                continue;
            }

            Contact contact{};

            if (GenerateSpherePlaneContact(
                sphereEntity,
                sphereTransform,
                sphereCollider,
                planeEntity,
                planeTransform,
                planeCollider,
                contact))
            {
                m_Contacts.push_back(contact);
            }
        }
    }
}

void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    // ========================================================================
    // Contact解決
    // ========================================================================
    // 現段階は各Contactを1回ずつ処理する最小構成です。
    // Sphereが複数面へ同時接触する場合や箱を積み上げる段階では、同じContact集合を
    // 複数回反復するSequential Impulse Solverへ発展させます。
    for (const Contact& contact : m_Contacts)
    {
        SolveContact(scene, contact, dt);
    }
}

void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    // ========================================================================
    // 簡易Sleep判定
    // ========================================================================
    // 現段階では線形速度のみで判定します。
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
        const float thresholdSquared = threshold * threshold;

        if (rigidBody.LinearVelocity.LengthSq() <= thresholdSquared)
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
    // ForceとTorqueは1 Stepだけ有効な蓄積値です。
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

void PhysicsWorld::AddImpulse(
    Scene& scene,
    Entity entity,
    const math::Vec3& impulse)
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

    // Impulseは時間積分を待たず、速度を直接変化させます。
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

    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
}

void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    // ========================================================================
    // 1回の固定Physics Step
    // ========================================================================
    //   1. 外力から速度更新
    //   2. Damping
    //   3. 更新済み速度から位置更新
    //   4. 移動後のColliderでContact生成
    //   5. Contactの位置・速度解決
    //   6. Sleep判定
    //   7. 一時Force/Torqueをクリア
    //
    // 衝突判定を位置更新後に行うことで、このStep中に発生した貫通を検出できます。
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
