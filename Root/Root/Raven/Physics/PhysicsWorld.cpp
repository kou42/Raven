
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

#include "Raven/Physics/PhysicsWorld.h"

namespace Raven
{

namespace ph // 物理演算のための名前空間
{

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
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        if (rigidBody.Type != BodyType::Dynamic) {
            continue;
        }

        if (rigidBody.UseGravity)
        {
            rigidBody.Force += m_Gravity * rigidBody.Mass;
        }

        const math::Vec3 acceleration = rigidBody.Force * rigidBody.InverseMass;

        rigidBody.LinearVelocity += acceleration * dt;
    }
}

void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        if (rigidBody.Type == BodyType::Static) {
            continue;
        }

        transform.Position += rigidBody.LinearVelocity * dt;
    }
}

void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
	// いったん中身は空です。将来的に角速度や回転の統合を追加する予定です。
}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    // いったん中身は空です。
}

void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    // いったん中身は空です。
}

void PhysicsWorld::ClearForces(Scene& scene)
{
    // いったん中身は空です。
}

void PhysicsWorld::AddForce(Scene& scene, Entity entity, const math::Vec3& force)
{
    // いったん中身は空です。
}

void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    // いったん中身は空です。
}

void PhysicsWorld::Step(Scene& scene, float dt)
{
    // もう少し高度になったら、、、
    /*  1. Physics Component同期
        2. 重力・外力適用
        3. Broad Phase
        4. Narrow Phase
        5. Contact生成
        6. Constraint Solver
        7. 速度更新
        8. 位置更新
        9. Sleep判定
        10. Collision Event発行
    */

    ApplyForces(scene, dt);
    IntegrateVelocities(scene, dt);

    DetectCollisions(scene);
    SolveCollisions(scene, dt);

    IntegratePositions(scene, dt);

    ClearForces(scene);
}

} // end ph

} //  end Raven
