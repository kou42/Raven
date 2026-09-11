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
    // 単発Stepの互換契約では、Simulation結果をその場で出力側へ反映します。
    StepSimulation(fixedDeltaTime);
    SynchronizeOutputs();
}

void SoftBodyWorld::StepSimulation(float fixedDeltaTime)
{
    // ========================================================================
    // SoftBody Simulation Phase
    // ========================================================================
    // Meshや具体的なCloth/Jelly型をPhysics Domainへ持ち込まず、登録済みParticipantだけを進めます。
    // catch-up時もここだけを複数回呼ぶことで、GPU更新を挟まずPhysics Stateを連続して積分できます。
    for (SoftBodySimulationParticipant* participant : m_SimulationParticipants)
    {
        if (participant == nullptr)
        {
            continue;
        }

        participant->SimulateSoftBody(fixedDeltaTime);
    }
}

void SoftBodyWorld::SynchronizeOutputs()
{
    // ========================================================================
    // Post-Simulation Output Synchronization
    // ========================================================================
    // 現在はMeshDeformerがこのhookを使ってParticle結果をMesh/GPUへ同期します。
    // Physics側は具体的な出力型を知らず、Participantの抽象境界だけを呼びます。
    // Sceneのcatch-up loop終了後に1回だけ呼ぶことで、途中Stateの不要なGPU uploadを避けます。
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
    // 単発Step利用側の互換性を維持し、Simulationと出力同期を連続して完了させます。
    StepSimulation(scene, fixedDeltaTime);
    SynchronizeOutputs();
}

void PhysicsSimulationWorld::StepSimulation(Scene& scene, float fixedDeltaTime)
{
    // Domain更新順序を上位Worldへ集約します。
    // Rigid Bodyを先に確定し、その後Soft Body Physics Stateだけを進めます。
    // Renderer出力同期を別Phaseにしたことで、catch-up中にGPU uploadを挟まずに済みます。
    m_RigidBodyWorld.Step(scene, fixedDeltaTime);
    m_SoftBodyWorld.StepSimulation(fixedDeltaTime);
}

void PhysicsSimulationWorld::SynchronizeOutputs()
{
    m_SoftBodyWorld.SynchronizeOutputs();
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
