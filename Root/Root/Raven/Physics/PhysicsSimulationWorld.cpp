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

bool SoftBodyWorld::RegisterSimulationParticipant(SoftBodySimulationParticipant& participant)
{
    if (ContainsSimulationParticipant(participant) == true)
    {
        return false;
    }

    m_SimulationParticipants.push_back(&participant);
    return true;
}

bool SoftBodyWorld::UnregisterSimulationParticipant(SoftBodySimulationParticipant& participant)
{
    const auto iterator = std::find(
        m_SimulationParticipants.begin(),
        m_SimulationParticipants.end(),
        &participant);

    if (iterator == m_SimulationParticipants.end())
    {
        return false;
    }

    m_SimulationParticipants.erase(iterator);
    return true;
}

void SoftBodyWorld::Clear()
{
    // RegistryはSolver/Participantを所有しないため、破棄は行わず参照だけを解除します。
    m_Solvers.clear();
    m_SimulationParticipants.clear();
}

void SoftBodyWorld::Step(float fixedDeltaTime)
{
    // ========================================================================
    // SoftBody Simulation Phase
    // ========================================================================
    // Meshや具体的なCloth/Jelly型をPhysics Domainへ持ち込まず、登録済みParticipantだけを進めます。
    // 全ParticipantのPhysics Stateを先に確定させてから出力同期へ進むことで、将来Soft-Soft Couplingを
    // 追加する場合もMesh更新がSimulation途中へ割り込まない順序を維持します。
    for (SoftBodySimulationParticipant* participant : m_SimulationParticipants)
    {
        if (participant == nullptr)
        {
            continue;
        }

        participant->SimulateSoftBody(fixedDeltaTime);
    }

    // ========================================================================
    // Post-Simulation Output Synchronization
    // ========================================================================
    // 現在はMeshDeformerがこのhookを使ってParticle結果をMesh/GPUへ同期します。
    // Physics側は具体的な出力型を知らず、Participantの抽象境界だけを呼びます。
    //
    // Fixed timestep catch-upで1frame中に複数Stepした場合は同期も複数回走ります。
    // 正しさを優先して1frame遅延を先に解消し、最終Step後だけ同期する最適化は後続で扱います。
    for (SoftBodySimulationParticipant* participant : m_SimulationParticipants)
    {
        if (participant == nullptr)
        {
            continue;
        }

        participant->SynchronizeSoftBodyOutput();
    }
}

bool SoftBodyWorld::ContainsSolver(const SoftBodySolver& solver) const
{
    return std::find(m_Solvers.begin(), m_Solvers.end(), &solver) != m_Solvers.end();
}

bool SoftBodyWorld::ContainsSimulationParticipant(const SoftBodySimulationParticipant& participant) const
{
    return std::find(
        m_SimulationParticipants.begin(),
        m_SimulationParticipants.end(),
        &participant) != m_SimulationParticipants.end();
}

void PhysicsSimulationWorld::Step(Scene& scene, float fixedDeltaTime)
{
    // Domain更新順序を上位Worldへ集約します。
    // 現段階ではRigid Bodyを先に確定し、その後Soft Body Simulationと出力同期を進めます。
    // 次PhaseのRigid-Soft Couplingでは、この境界の間にCollider同期/Impulse反映を挿入します。
    m_RigidBodyWorld.Step(scene, fixedDeltaTime);
    m_SoftBodyWorld.Step(fixedDeltaTime);
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
