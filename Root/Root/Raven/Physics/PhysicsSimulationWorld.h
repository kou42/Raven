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

// ============================================================================
// SoftBodyWorld
// ============================================================================
// SoftBody DomainをPhysicsSimulationWorld配下へ統合するための非所有Registryです。
// Solver参照はDebug/Coupling用、SimulationParticipant参照はFixed Step実行用として分離します。
// 所有権は従来どおりDeformer側に残し、MeshDeformationSystemが各frame先頭でRegistryを再構築します。
class SoftBodyWorld
{
public:
    bool RegisterSolver(SoftBodySolver& solver);
    bool UnregisterSolver(SoftBodySolver& solver);

    bool RegisterSimulationParticipant(SoftBodySimulationParticipant& participant);
    bool UnregisterSimulationParticipant(SoftBodySimulationParticipant& participant);

    // 互換入口です。単独Step時はSimulationと出力同期を連続実行します。
    void Step(float fixedDeltaTime);

    // Application frame内で複数Fixed Stepをcatch-upする場合、Simulationだけを繰り返し、
    // 最終Stateの出力同期を1回へ集約できるようPhaseを明示的に分離します。
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
    explicit FluidStaticColliderCouplingAccumulator(const std::vector<FluidCouplingBinding>& bindings)
        : m_Bindings(bindings)
    {
    }

    void SetSettings(const FluidStaticColliderCouplingSettings& settings) { m_Solver.SetSettings(settings); }

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
    explicit FluidRigidBodyCouplingAccumulator(const std::vector<FluidCouplingBinding>& bindings)
        : m_Bindings(bindings)
    {
    }

    void SetSettings(const FluidRigidBodyCouplingSettings& settings) { m_Solver.SetSettings(settings); }

    void ResolveScene(Scene& scene, PhysicsWorld& physicsWorld, std::vector<FluidParticle>& particles,
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

// ============================================================================
// FluidWorld
// ============================================================================
// Fluid DomainをPhysicsSimulationWorld配下へ統合するための非所有Registryです。
// SPH / PBF / FLIPなど具体的なSolverやParticle所有権はParticipant側へ残し、
// WorldはFixed Step実行順序、Application frame末尾の出力同期、Scene/RigidBodyとの
// Coupling実装を所有します。
//
// Coupling設定は各Fluid Participant側のBindingから登録時に取得します。
// Particle配列はParticipant所有のまま非所有参照し、Coupling実行順序だけをFluidWorldへ集約します。
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

    // 同じParticle配列へCouplingを二重適用しないよう、Particle配列を一意キーとして管理します。
    bool RegisterCouplingBinding(const FluidCouplingBinding& binding);
    bool UnregisterCouplingBinding(std::vector<FluidParticle>& particles);

    // 単独利用向けの互換入口です。Simulation後に出力同期まで完了します。
    // Sceneを持たないため、この入口ではCouplingを実行しません。
    void Step(float fixedDeltaTime);

    // Scene統合時の正式入口です。Participant Simulation後、同じFixed Step内で
    // Static Collider -> Dynamic RigidBodyの順にCouplingを解決します。
    void StepSimulation(Scene& scene, PhysicsWorld& physicsWorld, float fixedDeltaTime);

    // Physics単体TestなどSceneを持たない利用向けにSimulationだけを進めます。
    void StepSimulation(float fixedDeltaTime);
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
    // Participantの数値計算が確定したParticle状態へCouplingを適用します。
    // Domain間依存をDebug/Application Layerへ戻さず、Fluid fixed-step境界へ閉じ込めます。
    void ResolveCouplings(Scene& scene, PhysicsWorld& physicsWorld, float fixedDeltaTime);

private:
    std::vector<FluidSimulationParticipant*> m_SimulationParticipants;
    std::vector<FluidCouplingBinding> m_CouplingBindings;

    // Coupling SolverはFluidWorldが所有します。
    // Bindingごとの設定をResolve直前に反映し、Participant固有のParticle Radius等を維持します。
    FluidStaticColliderCouplingAccumulator m_StaticColliderCoupling;
    FluidRigidBodyCouplingAccumulator m_RigidBodyCoupling;
};

// ============================================================================
// RigidSoftSphereColliderBinding
// ============================================================================
// RigidBody側のSphere ColliderをSoftBody SolverのSphere Colliderへ同期するための非所有Bindingです。
// SourceRigidEntityはWorld-space Colliderの正規データ、TargetSoftBodyEntityはSolver local-spaceを
// 定義するTransform、TargetSolver/TargetColliderIndexは同期先を表します。
//
// TargetSolver + TargetColliderIndexはBinding Registry内で一意な同期先です。1つのSoft Colliderへ
// 複数Rigid Sourceを割り当てると、Collider状態の上書き順が不定になり、同じSoft反作用を複数Rigidへ
// 返してしまうため、PhysicsSimulationWorldは同一同期先への2件目の登録を拒否します。
//
// Soft -> Rigid反作用も同じ接触Pairに属するため、このBindingへ設定を集約します。
// ReactionImpulseScaleはSoftBody local-spaceで得た反作用をworld-spaceへ変換した後に掛ける係数です。
// 現段階ではRigid/Softの質量単位系が完全統一されていないため、Demo側で校正値を指定できます。
// MaximumReactionImpulseが0以下ならClampしません。
//
// Solverの所有権はDeformer側、Entityの所有権はScene側に残します。PhysicsSimulationWorldは
// Fixed Step境界で両Domainを接続する情報だけを保持し、Renderer/具体的なCloth型へ依存しません。
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
// Rigid Body / Fluid / Soft Body / ThermalのFixed Step順序をここへ集約し、後続のCoupling実装でも
// Scene/Game/RendererへDomain間依存を漏らさない構造を維持します。
class PhysicsSimulationWorld
{
public:
    // 単発実行向けの互換入口です。Simulation後に出力同期まで完了します。
    void Step(Scene& scene, float fixedDeltaTime);

    // Sceneのcatch-up loopから使用するSimulation専用入口です。
    void StepSimulation(Scene& scene, float fixedDeltaTime);
    void SynchronizeOutputs();

    // Rigid -> Soft Sphere Collider同期Bindingを登録します。
    // TargetSolver + TargetColliderIndexを一意な同期先として扱い、同一Soft Colliderへ
    // 2件目のBindingを登録しようとした場合はfalseを返して既存Bindingを維持します。
    bool RegisterRigidSoftSphereColliderBinding(const RigidSoftSphereColliderBinding& binding);
    bool UnregisterRigidSoftSphereColliderBinding(
        SoftBodySolver& targetSolver,
        uint32_t targetColliderIndex);
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
    // Rigid Body Stepで確定した最新Transform/ColliderをSoftBody local-spaceへ変換します。
    // SoftBody Step直前に呼ぶことで、同じFixed Step内で最新Rigid状態をCollision Constraintへ渡します。
    void SynchronizeRigidBodyCollidersToSoftBody(Scene& scene);

    // SoftBody Stepで生成されたTransientなSphere反作用をworld-spaceへ変換し、
    // 対応するDynamic RigidBodyへImpulseとして返します。
    // catch-up中も各Soft Step直後に適用するため、次のRigid substepから反作用を利用できます。
    void ApplySoftBodyReactionsToRigidBodies(Scene& scene);

private:
    PhysicsWorld m_RigidBodyWorld;
    FluidWorld m_FluidWorld;
    SoftBodyWorld m_SoftBodyWorld;
    ThermalWorld m_ThermalWorld;
    std::vector<RigidSoftSphereColliderBinding> m_RigidSoftSphereColliderBindings;
};

}
