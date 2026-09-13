#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Physics/Coupling/FluidRigidBodyCoupling.h"
#include "Raven/Physics/Coupling/FluidStaticColliderCoupling.h"
#include "Raven/Physics/Fluid/FluidCouplingBinding.h"
#include "Raven/Physics/Fluid/FluidSimulationParticipant.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/SoftBody/SoftBodySimulationParticipant.h"
#include "Raven/Physics/Thermal/ThermalWorld.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{
class Scene;

namespace ph
{
class SoftBodySolver;

class SoftBodyWorld
{
public:
    bool RegisterSolver(SoftBodySolver& solver);
    bool UnregisterSolver(SoftBodySolver& solver);
    bool RegisterSimulationParticipant(SoftBodySimulationParticipant& participant);
    bool UnregisterSimulationParticipant(SoftBodySimulationParticipant& participant);
    void Step(float fixedDeltaTime);
    void StepSimulation(float fixedDeltaTime);
    void SynchronizeOutputs();
    void Clear();
    bool ContainsSolver(const SoftBodySolver& solver) const;
    bool ContainsSimulationParticipant(const SoftBodySimulationParticipant& participant) const;
    std::size_t GetRegisteredSolverCount() const { return m_Solvers.size(); }
    std::size_t GetRegisteredSimulationParticipantCount() const { return m_SimulationParticipants.size(); }
    const std::vector<SoftBodySolver*>& GetRegisteredSolvers() const { return m_Solvers; }
    const std::vector<SoftBodySimulationParticipant*>& GetRegisteredSimulationParticipants() const
    {
        return m_SimulationParticipants;
    }

private:
    std::vector<SoftBodySolver*> m_Solvers;
    std::vector<SoftBodySimulationParticipant*> m_SimulationParticipants;
};

// ============================================================================
// FluidWorld
// ============================================================================
// Fluid DomainをPhysicsSimulationWorld配下へ統合する非所有Registryです。
// Solver/Particle所有権はParticipant側に残し、WorldはFixed Step実行順序、Coupling Phase、
// Application frame末尾の出力同期を統括します。
class FluidWorld
{
public:
    bool RegisterSimulationParticipant(FluidSimulationParticipant& participant);
    bool UnregisterSimulationParticipant(FluidSimulationParticipant& participant);

    // 同じParticle配列への二重Bindingは位置補正/Impulseの二重適用になるため拒否します。
    // Coupling設定はParticipant登録時の値をスナップショットとして保持します。
    bool RegisterCouplingBinding(const FluidCouplingBinding& binding);
    bool UnregisterCouplingBinding(std::vector<FluidParticle>& particles);

    // Scene非依存の単独利用向け入口です。Couplingは実行せずSimulationと出力同期だけを行います。
    void Step(float fixedDeltaTime);
    void StepSimulation(float fixedDeltaTime);

    // Scene統合時の正式入口です。
    // Fluid Simulation完了後、同じFixed Step内でStatic -> Dynamic Rigid Couplingを解決します。
    void StepSimulation(Scene& scene, PhysicsWorld& physicsWorld, float fixedDeltaTime);
    void SynchronizeOutputs();
    void Clear();

    bool ContainsSimulationParticipant(const FluidSimulationParticipant& participant) const;
    bool ContainsCouplingBinding(const std::vector<FluidParticle>& particles) const;

    std::size_t GetRegisteredSimulationParticipantCount() const
    {
        return m_SimulationParticipants.size();
    }

    std::size_t GetCouplingBindingCount() const
    {
        return m_CouplingBindings.size();
    }

    const std::vector<FluidSimulationParticipant*>& GetRegisteredSimulationParticipants() const
    {
        return m_SimulationParticipants;
    }

    const std::vector<FluidCouplingBinding>& GetCouplingBindings() const
    {
        return m_CouplingBindings;
    }

    const FluidStaticColliderCouplingStatistics& GetLastStaticColliderCouplingStatistics() const
    {
        return m_StaticColliderCoupling.GetLastStatistics();
    }

    const FluidRigidBodyCouplingStatistics& GetLastRigidBodyCouplingStatistics() const
    {
        return m_RigidBodyCoupling.GetLastStatistics();
    }

private:
    void ResolveCouplings(Scene& scene, PhysicsWorld& physicsWorld, float fixedDeltaTime);

private:
    std::vector<FluidSimulationParticipant*> m_SimulationParticipants;
    std::vector<FluidCouplingBinding> m_CouplingBindings;
    FluidStaticColliderCoupling m_StaticColliderCoupling{};
    FluidRigidBodyCoupling m_RigidBodyCoupling{};
};

struct RigidSoftSphereColliderBinding
{
    EntityHandle SourceRigidEntity{};
    EntityHandle TargetSoftBodyEntity{};
    SoftBodySolver* TargetSolver = nullptr;
    uint32_t TargetColliderIndex = 0u;
    bool ReactionEnabled = false;
    float ReactionImpulseScale = 1.0f;
    float MaximumReactionImpulse = 0.0f;
};

// ============================================================================
// PhysicsSimulationWorld
// ============================================================================
// Raven全体のPhysics Domainを統括する上位Worldです。
// Rigid Body / Fluid / Soft Body / ThermalのFixed Step順序をここへ集約します。
class PhysicsSimulationWorld
{
public:
    void Step(Scene& scene, float fixedDeltaTime);
    void StepSimulation(Scene& scene, float fixedDeltaTime);
    void SynchronizeOutputs();

    bool RegisterRigidSoftSphereColliderBinding(const RigidSoftSphereColliderBinding& binding);
    bool UnregisterRigidSoftSphereColliderBinding(SoftBodySolver& targetSolver, uint32_t targetColliderIndex);
    void ClearRigidSoftSphereColliderBindings();

    std::size_t GetRigidSoftSphereColliderBindingCount() const
    {
        return m_RigidSoftSphereColliderBindings.size();
    }

    PhysicsWorld& GetRigidBodyWorld();
    const PhysicsWorld& GetRigidBodyWorld() const;
    FluidWorld& GetFluidWorld();
    const FluidWorld& GetFluidWorld() const;
    SoftBodyWorld& GetSoftBodyWorld();
    const SoftBodyWorld& GetSoftBodyWorld() const;
    ThermalWorld& GetThermalWorld();
    const ThermalWorld& GetThermalWorld() const;

private:
    void SynchronizeRigidBodyCollidersToSoftBody(Scene& scene);
    void ApplySoftBodyReactionsToRigidBodies(Scene& scene);

private:
    PhysicsWorld m_RigidBodyWorld;
    FluidWorld m_FluidWorld;
    SoftBodyWorld m_SoftBodyWorld;
    ThermalWorld m_ThermalWorld;
    std::vector<RigidSoftSphereColliderBinding> m_RigidSoftSphereColliderBindings;
};

}
