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
// Fluid Coupling Statistics Accumulator
// ============================================================================
// FluidWorldは同じCoupling SolverをBindingごとに設定し直して利用します。Solver単体の
// LastStatisticsはResolveSceneごとに初期化されるため、そのまま公開すると最後のBindingだけが
// 見えてしまいます。このAdapterは既存Solver APIを維持したまま、登録順の先頭Bindingを
// fixed-step境界として検出し、全Bindingの診断値を加算します。
class FluidStaticColliderCouplingAccumulator
{
public:
    explicit FluidStaticColliderCouplingAccumulator(
        const std::vector<FluidCouplingBinding>& bindings)
        : m_Bindings(bindings)
    {
    }

    void SetSettings(const FluidStaticColliderCouplingSettings& settings)
    {
        m_Solver.SetSettings(settings);
    }

    void ResolveScene(Scene& scene, std::vector<FluidParticle>& particles)
    {
        if (IsFirstEnabledBinding(particles) == true)
        {
            m_AggregatedStatistics = {};
        }

        m_Solver.ResolveScene(scene, particles);
        const FluidStaticColliderCouplingStatistics& statistics = m_Solver.GetLastStatistics();
        m_AggregatedStatistics.SupportedColliderCount += statistics.SupportedColliderCount;
        m_AggregatedStatistics.CandidatePairCount += statistics.CandidatePairCount;
        m_AggregatedStatistics.ResolvedContactCount += statistics.ResolvedContactCount;
    }

    const FluidStaticColliderCouplingStatistics& GetLastStatistics() const
    {
        if (HasEnabledBinding() == false)
        {
            static const FluidStaticColliderCouplingStatistics EmptyStatistics{};
            return EmptyStatistics;
        }
        return m_AggregatedStatistics;
    }

private:
    bool HasEnabledBinding() const
    {
        for (const FluidCouplingBinding& binding : m_Bindings)
        {
            if (binding.Particles != nullptr && binding.StaticColliderCouplingEnabled == true)
            {
                return true;
            }
        }
        return false;
    }

    bool IsFirstEnabledBinding(const std::vector<FluidParticle>& particles) const
    {
        for (const FluidCouplingBinding& binding : m_Bindings)
        {
            if (binding.Particles == nullptr || binding.StaticColliderCouplingEnabled == false)
            {
                continue;
            }
            return binding.Particles == &particles;
        }
        return false;
    }

private:
    const std::vector<FluidCouplingBinding>& m_Bindings;
    FluidStaticColliderCoupling m_Solver{};
    FluidStaticColliderCouplingStatistics m_AggregatedStatistics{};
};

class FluidRigidBodyCouplingAccumulator
{
public:
    explicit FluidRigidBodyCouplingAccumulator(
        const std::vector<FluidCouplingBinding>& bindings)
        : m_Bindings(bindings)
    {
    }

    void SetSettings(const FluidRigidBodyCouplingSettings& settings)
    {
        m_Solver.SetSettings(settings);
    }

    void ResolveScene(
        Scene& scene,
        PhysicsWorld& physicsWorld,
        std::vector<FluidParticle>& particles,
        float fixedDeltaTime)
    {
        if (IsFirstEnabledBinding(particles) == true)
        {
            m_AggregatedStatistics = {};
        }

        m_Solver.ResolveScene(scene, physicsWorld, particles, fixedDeltaTime);
        const FluidRigidBodyCouplingStatistics& statistics = m_Solver.GetLastStatistics();
        m_AggregatedStatistics.DynamicBodyCount += statistics.DynamicBodyCount;
        m_AggregatedStatistics.CandidatePairCount += statistics.CandidatePairCount;
        m_AggregatedStatistics.ResolvedContactCount += statistics.ResolvedContactCount;
        m_AggregatedStatistics.AppliedImpulseCount += statistics.AppliedImpulseCount;
        m_AggregatedStatistics.AppliedDragImpulseCount += statistics.AppliedDragImpulseCount;
        m_AggregatedStatistics.AppliedPressureImpulseCount += statistics.AppliedPressureImpulseCount;
        m_AggregatedStatistics.AppliedBuoyancyImpulseCount += statistics.AppliedBuoyancyImpulseCount;
        m_AggregatedStatistics.TotalNormalImpulse += statistics.TotalNormalImpulse;
        m_AggregatedStatistics.TotalDragImpulse += statistics.TotalDragImpulse;
        m_AggregatedStatistics.TotalPressureImpulse += statistics.TotalPressureImpulse;
        m_AggregatedStatistics.TotalBuoyancyImpulse += statistics.TotalBuoyancyImpulse;
        m_AggregatedStatistics.TotalDisplacedFluidMass += statistics.TotalDisplacedFluidMass;
    }

    const FluidRigidBodyCouplingStatistics& GetLastStatistics() const
    {
        if (HasEnabledBinding() == false)
        {
            static const FluidRigidBodyCouplingStatistics EmptyStatistics{};
            return EmptyStatistics;
        }
        return m_AggregatedStatistics;
    }

private:
    bool HasEnabledBinding() const
    {
        for (const FluidCouplingBinding& binding : m_Bindings)
        {
            if (binding.Particles != nullptr && binding.RigidBodyCouplingEnabled == true)
            {
                return true;
            }
        }
        return false;
    }

    bool IsFirstEnabledBinding(const std::vector<FluidParticle>& particles) const
    {
        for (const FluidCouplingBinding& binding : m_Bindings)
        {
            if (binding.Particles == nullptr || binding.RigidBodyCouplingEnabled == false)
            {
                continue;
            }
            return binding.Particles == &particles;
        }
        return false;
    }

private:
    const std::vector<FluidCouplingBinding>& m_Bindings;
    FluidRigidBodyCoupling m_Solver{};
    FluidRigidBodyCouplingStatistics m_AggregatedStatistics{};
};

class FluidWorld
{
public:
    FluidWorld()
        : m_StaticColliderCoupling(m_CouplingBindings)
        , m_RigidBodyCoupling(m_CouplingBindings)
    {
    }

    bool RegisterSimulationParticipant(FluidSimulationParticipant& participant);
    bool UnregisterSimulationParticipant(FluidSimulationParticipant& participant);
    bool RegisterCouplingBinding(const FluidCouplingBinding& binding);
    bool UnregisterCouplingBinding(std::vector<FluidParticle>& particles);
    void Step(float fixedDeltaTime);
    void StepSimulation(Scene& scene, PhysicsWorld& physicsWorld, float fixedDeltaTime);
    void StepSimulation(float fixedDeltaTime);
    void SynchronizeOutputs();
    void Clear();
    bool ContainsSimulationParticipant(const FluidSimulationParticipant& participant) const;
    bool ContainsCouplingBinding(const std::vector<FluidParticle>& particles) const;

    std::size_t GetRegisteredSimulationParticipantCount() const { return m_SimulationParticipants.size(); }
    std::size_t GetCouplingBindingCount() const { return m_CouplingBindings.size(); }
    const std::vector<FluidSimulationParticipant*>& GetRegisteredSimulationParticipants() const
    {
        return m_SimulationParticipants;
    }
    const std::vector<FluidCouplingBinding>& GetCouplingBindings() const { return m_CouplingBindings; }

    // 直近のFluid fixed-stepで有効な全Bindingに対して発生したCoupling診断値の合計です。
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
    FluidStaticColliderCouplingAccumulator m_StaticColliderCoupling;
    FluidRigidBodyCouplingAccumulator m_RigidBodyCoupling;
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

class PhysicsSimulationWorld
{
public:
    void Step(Scene& scene, float fixedDeltaTime);
    void StepSimulation(Scene& scene, float fixedDeltaTime);
    void SynchronizeOutputs();
    bool RegisterRigidSoftSphereColliderBinding(const RigidSoftSphereColliderBinding& binding);
    bool UnregisterRigidSoftSphereColliderBinding(SoftBodySolver& targetSolver, uint32_t targetColliderIndex);
    void ClearRigidSoftSphereColliderBindings();
    std::size_t GetRigidSoftSphereColliderBindingCount() const { return m_RigidSoftSphereColliderBindings.size(); }
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
