#include "Raven/Physics/PhysicsSimulationWorld.h"

#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

void PhysicsSimulationWorld::Step(Scene& scene, float fixedDeltaTime)
{
    // Phase 0では既存Rigid Body WorldへそのままStepを委譲します。
    // 上位Worldに時間更新の入口を集約しておくことで、将来Domainが増えた際に
    // Scene側を肥大化させず、Physics内で更新順序とCouplingを管理できます。
    m_RigidBodyWorld.Step(scene, fixedDeltaTime);
}

PhysicsWorld& PhysicsSimulationWorld::GetRigidBodyWorld()
{
    return m_RigidBodyWorld;
}

const PhysicsWorld& PhysicsSimulationWorld::GetRigidBodyWorld() const
{
    return m_RigidBodyWorld;
}

}
