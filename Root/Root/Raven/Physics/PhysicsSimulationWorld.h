#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/SoftBody/SoftBodySimulationParticipant.h"
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
// RigidSoftSphereColliderBinding
// ============================================================================
// RigidBody側のSphere ColliderをSoftBody SolverのSphere Colliderへ同期するための非所有Bindingです。
// SourceRigidEntityはWorld-space Colliderの正規データ、TargetSoftBodyEntityはSolver local-spaceを
// 定義するTransform、TargetSolver/TargetColliderIndexは同期先を表します。
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
// Rigid BodyとSoft BodyのFixed Step順序をここへ集約し、後続のCoupling実装でも
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
    // 同一Source/Target/Colliderの重複登録はfalseを返し、既存Bindingを維持します。
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

    SoftBodyWorld& GetSoftBodyWorld();
    const SoftBodyWorld& GetSoftBodyWorld() const;

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
    SoftBodyWorld m_SoftBodyWorld;
    std::vector<RigidSoftSphereColliderBinding> m_RigidSoftSphereColliderBindings;
};

}
}
