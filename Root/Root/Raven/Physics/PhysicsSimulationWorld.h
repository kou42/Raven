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

    // 直近のFluid fixed-stepで全Bindingに対して発生したCoupling診断値の合計です。
    // Solver単体のLastStatisticsを直接公開すると最後に処理したBindingだけが見えるため、
    // World境界では複数Fluid Domainを横断した値へ集約して公開します。
    const FluidStaticColliderCouplingStatistics& GetLastStaticColliderCouplingStatistics() const
    {
        return m_LastStaticColliderCouplingStatistics;
    }

    const FluidRigidBodyCouplingStatistics& GetLastRigidBodyCouplingStatistics() const
    {
        return m_LastRigidBodyCouplingStatistics;
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
    FluidStaticColliderCoupling m_StaticColliderCoupling{};
    FluidRigidBodyCoupling m_RigidBodyCoupling{};

    // SolverのLastStatisticsはBindingごとに上書きされるため、FluidWorldがFixed Step開始時に
    // 0へ戻して各Bindingの結果を加算します。Debug HUDやProfilerはこのWorld集約値を参照します。
    FluidStaticColliderCouplingStatistics m_LastStaticColliderCouplingStatistics{};
    FluidRigidBodyCouplingStatistics m_LastRigidBodyCouplingStatistics{};
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
