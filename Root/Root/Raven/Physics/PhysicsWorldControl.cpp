#include "Raven/Physics/PhysicsWorld.h"

#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace ph
{
namespace
{
RigidBodyComponent* TryGetAliveRigidBody(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity)) return nullptr;
    return scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
}

const RigidBodyComponent* TryGetAliveRigidBody(const Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity)) return nullptr;
    return scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
}

TransformComponent* TryGetAliveTransform(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity)) return nullptr;
    return scene.TryGetComponent<TransformComponent>(entity.GetIndex());
}

void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}
} // namespace

void PhysicsWorld::AddTorque(Scene& scene, Entity entity, const math::Vec3& torque)
{
    RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f)
        return;

    WakeRigidBody(*rigidBody);
    // TorqueはForceと同様に1 Step分を蓄積し、ApplyForces()で
    // I_world^-1 * Torque としてAngularVelocityへ積分されます。
    rigidBody->Torque += torque;
}

void PhysicsWorld::AddImpulseAtPoint(Scene& scene, Entity entity, const math::Vec3& impulse,
    const math::Vec3& worldPoint)
{
    RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    TransformComponent* transform = TryGetAliveTransform(scene, entity);
    ColliderComponent* collider = scene.IsEntityAlive(entity)
        ? scene.TryGetComponent<ColliderComponent>(entity.GetIndex()) : nullptr;

    if (rigidBody == nullptr || transform == nullptr || collider == nullptr
        || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f)
        return;

    WakeRigidBody(*rigidBody);
    EnsurePhysicsOrientation(*transform, *rigidBody);

    // Linear impulseは通常のAddImpulseと同じです。
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;

    // ------------------------------------------------------------------------
    // Angular impulse
    // ------------------------------------------------------------------------
    // r: 剛体重心からImpulse作用点までのworld-space腕ベクトル
    // L = r x J: Impulseによる角運動量変化
    // deltaOmega = I_world^-1 * L
    //
    // 作用点が重心ならr=0となり角速度は変化しません。箱の端を押す・爆発で
    // オブジェクトを回す、といったゲーム側の操作をCollision Solverと同じ式で扱えます。
    const math::Vec3 r = worldPoint - transform->Position;
    const math::Vec3 angularImpulse = math::Vec3::Cross(r, impulse);
    const math::Mat3 inverseInertia = ComputeWorldInverseInertia(transform, rigidBody, collider);
    rigidBody->AngularVelocity += inverseInertia * angularImpulse;
}

void PhysicsWorld::SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity)
{
    RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    if (rigidBody == nullptr || rigidBody->Type == BodyType::Static) return;
    rigidBody->LinearVelocity = velocity;
    if (rigidBody->Type == BodyType::Dynamic) WakeRigidBody(*rigidBody);
}

math::Vec3 PhysicsWorld::GetLinearVelocity(const Scene& scene, Entity entity) const
{
    const RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    return rigidBody ? rigidBody->LinearVelocity : math::Vec3{};
}

void PhysicsWorld::Teleport(Scene& scene, Entity entity, const math::Vec3& position)
{
    TransformComponent* transform = TryGetAliveTransform(scene, entity);
    if (transform == nullptr) return;
    transform->Position = position;

    RigidBodyComponent* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody != nullptr && rigidBody->Type == BodyType::Dynamic) WakeRigidBody(*rigidBody);
}

void PhysicsWorld::MovePosition(Scene& scene, Entity entity, const math::Vec3& position)
{
    TransformComponent* transform = TryGetAliveTransform(scene, entity);
    RigidBodyComponent* rigidBody = TryGetAliveRigidBody(scene, entity);
    if (transform == nullptr || rigidBody == nullptr || rigidBody->Type != BodyType::Kinematic) return;
    transform->Position = position;
}

} // namespace ph
} // namespace Raven
