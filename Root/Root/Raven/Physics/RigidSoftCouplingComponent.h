#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven::ph
{

// ============================================================================
// RigidSoftCouplingComponent
// ============================================================================
// SoftBodyを持つEntityへ付与し、RigidBody Sphereとの双方向連成設定をECSへ公開します。
//
// Target SoftBody EntityはこのComponent自身を所有するEntityです。SourceRigidEntityだけを明示的に
// 保持することで、Solver pointerやCollider IndexのようなRuntime情報をSceneデータへ保存しません。
// それらはMeshDeformationSystemがDeformer初期化後に解決し、PhysicsSimulationWorldの
// 非所有Binding Registryを毎Game Update再構築します。
//
// この構成によりEntity/Deformer破棄時の手動Unregisterが不要になり、Demo LayerやGameplay Layerが
// PhysicsSimulationWorldのBinding lifetimeを直接管理する必要もなくなります。
struct RigidSoftCouplingComponent
{
    EntityHandle SourceRigidEntity{};

    bool Enabled = true;
    bool ReactionEnabled = false;
    float ReactionImpulseScale = 1.0f;
    float MaximumReactionImpulse = 0.0f;
};

} // namespace Raven::ph
