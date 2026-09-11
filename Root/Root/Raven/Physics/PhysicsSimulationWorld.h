#pragma once

#include "Raven/Physics/PhysicsWorld.h"

namespace Raven
{
class Scene;

namespace ph
{
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

private:
    // 既存PhysicsWorldの所有権をここへ移すだけに留めます。
    // Phase 0ではRigid Bodyの内部実装や公開APIを変更しないことで、
    // Character / Ragdoll / Physics Queryなど既存利用側との互換性を維持します。
    PhysicsWorld m_RigidBodyWorld;
};

}
}
