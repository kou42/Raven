// Raven/Physics/Ragdoll/RagdollPhysicsBridge.h
#pragma once

#include <string>
#include <vector>

#include "Raven/Physics/Ragdoll/RagdollRuntime.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Scene;

// ============================================================================
// RagdollPhysicsBodyBinding
// ============================================================================
// 1つのRagdoll BoneとScene上のPhysics Entityを対応付けます。
//
// RagdollRuntimeはPhysics実装を知らず、Scene / PhysicsWorldもSkeletonを知りません。
// このBindingを境界に置くことで、両者の責務を混ぜずにBody状態だけ同期できます。
struct RagdollPhysicsBodyBinding
{
    BoneIndex Bone = InvalidBoneIndex;
    Entity BodyEntity{};
};

// ============================================================================
// RagdollPhysicsBridge
// ============================================================================
// RagdollRuntimeのBodyをScene上の実RigidBodyへ変換するBridgeです。
//
// 処理順:
// 1. RagdollRuntime::CaptureAnimationPose() / SampleAnimationPose()でAnimation状態をBodyへ取り込む
// 2. CreateBodies()でScene Entity + Dynamic RigidBody + Capsule Colliderを生成する
// 3. PhysicsWorld::Step()で重力・衝突を解く
// 4. SyncPhysicsToRagdoll()で結果をRagdollRuntimeへ戻す
// 5. RagdollConstraintSolverで関節を補正する
// 6. 必要ならSyncRagdollToPhysics()でConstraint結果をPhysics Bodyへ戻す
// 7. SkeletonPoseへ反映する
//
// ColliderはRagdollBodyDefinition::Radius / HalfLengthをCapsuleへ直接渡します。
// Capsuleの長軸はBoneローカル+Yで、Transform / RigidBody Orientationの回転に追従します。
class RagdollPhysicsBridge
{
public:
    bool CreateBodies(
        Scene& scene,
        RagdollRuntime& ragdoll,
        const std::string& entityNamePrefix = "Ragdoll",
        std::string* errorMessage = nullptr);

    // Bridgeが生成したPhysics EntityをSceneから破棄予約します。
    // Scene::QueueDestroyEntity()を使うため、Physics/Scene反復中でも安全に呼べます。
    void DestroyBodies(Scene& scene);

    // Ragdoll Body State -> Scene Physics Components
    // Ragdoll開始時やConstraint Projection後の再同期に使用します。
    bool SyncRagdollToPhysics(
        Scene& scene,
        const RagdollRuntime& ragdoll,
        bool syncVelocities = true,
        std::string* errorMessage = nullptr) const;

    // Scene Physics Components -> Ragdoll Body State
    // PhysicsWorld::Step()後に呼び、重力・衝突結果をSkeleton側へ渡します。
    bool SyncPhysicsToRagdoll(
        const Scene& scene,
        RagdollRuntime& ragdoll,
        std::string* errorMessage = nullptr) const;

    bool IsCreated() const
    {
        return m_Bindings.empty() == false;
    }

    const std::vector<RagdollPhysicsBodyBinding>& GetBindings() const
    {
        return m_Bindings;
    }

    Entity FindEntity(BoneIndex boneIndex) const;

private:
    const RagdollBodyDefinition* FindBodyDefinition(
        const RagdollDefinition& definition,
        BoneIndex boneIndex,
        const Skeleton& skeleton) const;

private:
    std::vector<RagdollPhysicsBodyBinding> m_Bindings;
};

} // namespace Raven
