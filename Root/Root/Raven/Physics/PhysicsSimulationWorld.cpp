#include "Raven/Physics/PhysicsSimulationWorld.h"

#include <algorithm>

#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{

bool SoftBodyWorld::RegisterSolver(SoftBodySolver& solver)
{
    if (ContainsSolver(solver) == true)
    {
        return false;
    }

    m_Solvers.push_back(&solver);
    return true;
}

bool SoftBodyWorld::UnregisterSolver(SoftBodySolver& solver)
{
    const auto iterator = std::find(m_Solvers.begin(), m_Solvers.end(), &solver);
    if (iterator == m_Solvers.end())
    {
        return false;
    }

    m_Solvers.erase(iterator);
    return true;
}

void SoftBodyWorld::Clear()
{
    // RegistryはSolverを所有しないため、破棄は行わず参照だけを解除します。
    m_Solvers.clear();
}

bool SoftBodyWorld::ContainsSolver(const SoftBodySolver& solver) const
{
    return std::find(m_Solvers.begin(), m_Solvers.end(), &solver) != m_Solvers.end();
}

void PhysicsSimulationWorld::Step(Scene& scene, float fixedDeltaTime)
{
    // Phase 0では既存Rigid Body WorldへそのままStepを委譲します。
    // SoftBodyは現在MeshDeformationSystem側で更新されているため、ここからはまだStepしません。
    // 更新責務をPhysics側へ移管するPhaseで、この関数へDomain順序を集約します。
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

SoftBodyWorld& PhysicsSimulationWorld::GetSoftBodyWorld()
{
    return m_SoftBodyWorld;
}

const SoftBodyWorld& PhysicsSimulationWorld::GetSoftBodyWorld() const
{
    return m_SoftBodyWorld;
}

}
