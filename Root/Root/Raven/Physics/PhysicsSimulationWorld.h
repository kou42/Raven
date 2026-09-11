#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Physics/PhysicsWorld.h"

namespace Raven
{
class Scene;

namespace ph
{
class SoftBodySolver;

// ============================================================================
// SoftBodyWorld
// ============================================================================
// SoftBody DomainをPhysicsSimulationWorld配下へ段階的に統合するためのRegistryです。
//
// 現在のCloth / JellyはRenderer側DeformerがSoftBodySolverを所有し、
// MeshDeformationSystemからStepしています。ここで同じSolverをStepすると二重積分になるため、
// Phase 2の第一段階では所有権を奪わず、Solver参照の登録・列挙だけを担当します。
//
// 登録pointerは非所有です。所有者は破棄前に必ずUnregisterSolver()を呼ぶ必要があります。
// 後続PhaseでSimulation更新をPhysics側へ移管した後も、この境界をDomain管理の入口として利用します。
class SoftBodyWorld
{
public:
    bool RegisterSolver(SoftBodySolver& solver);
    bool UnregisterSolver(SoftBodySolver& solver);
    void Clear();

    bool ContainsSolver(const SoftBodySolver& solver) const;
    std::size_t GetRegisteredSolverCount() const { return m_Solvers.size(); }
    const std::vector<SoftBodySolver*>& GetRegisteredSolvers() const { return m_Solvers; }

private:
    std::vector<SoftBodySolver*> m_Solvers;
};

// ============================================================================
// PhysicsSimulationWorld
// ============================================================================
// Raven全体のPhysics Domainを統括するための上位Worldです。
//
// Phase 0では既存のPhysicsWorldをRigid Body Domainとしてそのまま保持し、
// 衝突判定・Solver・Queryなどの既存挙動は変更しません。
// 将来SoftBody / Fluid / ThermalなどをSceneへ直接追加するのではなく、
// このWorldを入口としてDomainごとの更新順序やCouplingを管理できる構造へ
// 段階的に拡張することを目的とします。
class PhysicsSimulationWorld
{
public:
    void Step(Scene& scene, float fixedDeltaTime);

    PhysicsWorld& GetRigidBodyWorld();
    const PhysicsWorld& GetRigidBodyWorld() const;

    SoftBodyWorld& GetSoftBodyWorld();
    const SoftBodyWorld& GetSoftBodyWorld() const;

private:
    // 既存PhysicsWorldの所有権をここへ移すだけに留めます。
    // Phase 0ではRigid Bodyの内部実装や公開APIを変更しないことで、
    // Character / Ragdoll / Physics Queryなど既存利用側との互換性を維持します。
    PhysicsWorld m_RigidBodyWorld;

    // SoftBody Solver本体の所有権・Step責務はまだDeformer側に残します。
    // ここでは将来のDomain更新順序とRigid/Soft Couplingを管理するためのRegistryだけを保持します。
    SoftBodyWorld m_SoftBodyWorld;
};

}
}
