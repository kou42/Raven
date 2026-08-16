// Raven/Gltf/SkinnedRagdollRuntime.h
#pragma once

#include <cstddef>
#include <string>

#include "Raven/Physics/Ragdoll/RagdollConstraintSolver.h"
#include "Raven/Physics/Ragdoll/RagdollPhysicsBridge.h"
#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

namespace Raven
{
class Scene;

namespace Gltf
{

class SkinnedMeshRuntimeAsset;

// ============================================================================
// SkinnedRagdollRuntime
// ============================================================================
// glTF SkinnedMesh RuntimeとRagdoll / PhysicsWorldを接続するためのBridgeです。
//
// このクラス自身が物理計算やBone変換アルゴリズムを持つのではなく、各責務を次のように分離します。
//
// - RagdollRuntime
//     Skeleton BoneとRagdoll Bodyの対応、およびSkeletonPose <-> RagdollBodyState変換を担当します。
// - RagdollConstraintSolver
//     Parent / Child Body間のBall/Socket接続とSwing/Twist角度制限を担当します。
// - RagdollPhysicsBridge
//     RagdollBodyStateとScene上のRigidBodyComponent / ColliderComponentを相互同期します。
// - SkinnedRagdollRuntime
//     上記3要素をSkinIndex単位でまとめ、Body / Clothesなど同一Skinを使う全Primitiveへ
//     最終SkeletonPoseを配布するオーケストレーションを担当します。
//
// 重要:
// Animation / glTF / Physicsの責務をこのクラス1つへ集中させないことが設計上の目的です。
// Physics実装やCollider形状を変更しても、Skeleton Pose同期側へ影響を広げない構成にします。
class SkinnedRagdollRuntime
{
public:
    // 指定SkinIndexのSkeletalMeshDeformerを検索し、RagdollDefinitionからRuntimeを構築します。
    // 同一Skinを共有するPrimitiveはBone IndexでPoseをコピーするため、Skeleton構造の一致も検証します。
    //
    // Physics Body生成後の再AttachはScene上に孤児Entityを残す危険があるため禁止しています。
    // 再Attachする場合は先にDestroyPhysicsBodies()を呼んでください。
    bool Attach(
        std::size_t skinIndex,
        SkinnedMeshRuntimeAsset& targetAsset,
        const RagdollDefinition& definition,
        std::string* errorMessage = nullptr);

    // ========================================================================
    // Animation -> Ragdoll Velocity Sampling
    // ========================================================================
    // Animation駆動中、Animation評価後に毎Frame呼びます。
    // 現在Poseと前Frame Poseから各Ragdoll Bodyのworld Linear / Angular Velocityを計測し、
    // EnterRagdoll() -> CreatePhysicsBodies()時にRigidBodyへそのまま継承できる状態を作ります。
    //
    // 典型的な呼び出し順:
    //   Animation Update
    //     -> SampleAnimationMotion(deltaTime)
    //     -> 必要なFrameで EnterRagdoll()
    //     -> InitializeConstraints()
    //     -> CreatePhysicsBodies()
    //
    // ClipのTime Seek / Teleportなど不連続なPose変更を行った場合は
    // ResetAnimationMotionHistory()を先に呼び、見かけ上の巨大速度をRagdollへ渡さないようにします。
    bool SampleAnimationMotion(
        float deltaTime,
        std::string* errorMessage = nullptr);

    void ResetAnimationMotionHistory();

    // 現在のDeformer PoseをRagdoll Bodyの初期Global Poseとして取り込みます。
    // Animation Runtime更新後、Physics Simulationへ制御を渡す直前に呼びます。
    //
    // SampleAnimationMotion()を継続して呼んでいる場合、RagdollRuntimeには既に最新Poseと
    // Animation由来Velocityが入っているため、その値を維持してPhysicsへ制御を渡します。
    // Samplingを使っていない従来経路では現在PoseをCaptureし、Velocity=0から開始します。
    bool EnterRagdoll(std::string* errorMessage = nullptr);

    // EnterRagdoll()で取り込んだ現在PoseをJointの基準姿勢としてConstraintを初期化します。
    // AnimationからRagdollへ切り替えるたびに呼ぶことで、その瞬間の姿勢をRest Poseとして扱い、
    // Constraint初期化直後にT-Pose等へ引き戻されることを防ぎます。
    bool InitializeConstraints(std::string* errorMessage = nullptr);

    // EnterRagdoll()後のBody PoseからScene上にDynamic RigidBody / Capsule Collider Entityを生成します。
    // SceneのPhysicsWorldはECS Componentを直接走査するため、生成後は通常のPhysics Step対象になります。
    // RagdollBodyDefinitionのRadius / HalfLengthをCapsuleへ直接渡すため、Box近似は行いません。
    bool CreatePhysicsBodies(
        Scene& scene,
        const std::string& entityNamePrefix = "Ragdoll",
        std::string* errorMessage = nullptr);

    // CreatePhysicsBodies()で生成したBone用Physics Entityを破棄予約します。
    // Scene走査中でも安全に扱えるよう、実際の破棄方法はRagdollPhysicsBridge / Scene側へ委譲します。
    void DestroyPhysicsBodies(Scene& scene);

    // Physics側などからBoneIndex指定でBody状態を直接更新します。
    // PhysicsWorldを介さないテストや外部Physics Backend接続にも利用できる低レベルAPIです。
    bool SetBodyState(
        BoneIndex boneIndex,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    // Bone名指定版です。AssetごとのBoneIndexを呼び出し側へ露出させたくない場合に使用します。
    bool SetBodyState(
        const std::string& boneName,
        const RagdollBodyState& state,
        std::string* errorMessage = nullptr);

    // Physics積分後のBody StateへBall/Socket + Swing/Twist制約を反復適用します。
    // 現在はRagdollBodyState上のProjection Solverであり、Angular Impulse Solverではありません。
    bool SolveConstraints(
        const RagdollConstraintSolverSettings& settings = {},
        std::string* errorMessage = nullptr);

    // PhysicsWorld::Step()後のScene RigidBody状態をRagdoll Runtimeへ取り込みます。
    // Position / OrientationだけでなくLinear / Angular VelocityもRagdollBodyStateへ同期します。
    bool SyncPhysicsToRagdoll(
        const Scene& scene,
        std::string* errorMessage = nullptr);

    // Constraint Projection後のRagdoll Body PoseをScene RigidBodyへ戻します。
    // Position / Orientationを同期し、次FrameのPhysics開始状態をJoint制約と一致させます。
    //
    // syncVelocities=falseではPhysicsWorldの衝突応答で得た速度を保持し、姿勢だけ補正します。
    // 現在のProjection Constraint利用時は通常falseを使用します。
    bool SyncRagdollToPhysics(
        Scene& scene,
        bool syncVelocities = false,
        std::string* errorMessage = nullptr);

    // 現在Body PoseをSkeletonへ反映し、同じSkinを使う全Primitiveへ完成Poseを配布します。
    // weight=1で完全Ragdoll、0～1でAnimation Poseとの復帰Blendとして利用できます。
    //
    // Body / ClothesをPrimitiveごとに別々にRagdoll変換せず、1つの完成Poseを共有することで
    // 同一Skinを利用するMesh間のBone Poseずれを防ぎます。
    bool ApplyPose(
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    // PhysicsWorld::Step()後に呼ぶ、Physics駆動Ragdollの統合入口です。
    //
    // 処理順:
    // Physics Entity -> RagdollBodyState -> Joint Constraint -> Physics Entityへ姿勢再同期
    // -> SkeletonPose -> 同一Skin Primitive -> CPU Skinning / GPU同期
    //
    // Joint Constraintの補正結果をPhysics側へ戻すことで、表示Poseと次Physics Stepの開始Poseを一致させます。
    bool UpdatePhysicsDrivenMesh(
        Scene& scene,
        float deltaTime,
        const RagdollConstraintSolverSettings& settings = {},
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    // PhysicsWorldを介さず、現在RagdollBodyStateへConstraintを適用してMeshまで更新する利便APIです。
    // Constraint Solver単体の検証や外部Physics BackendからSetBodyState()する場合に使用できます。
    bool UpdateConstrainedMesh(
        float deltaTime,
        const RagdollConstraintSolverSettings& settings = {},
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    // Constraintを使わず、現在Body PoseをそのままSkeleton / Meshへ反映する従来APIです。
    // Pose変換だけを検証したい場合や、Constraintを外部で解決済みの場合に使用します。
    bool UpdateMesh(
        float deltaTime,
        float weight = 1.0f,
        std::string* errorMessage = nullptr);

    RagdollRuntime& GetRagdoll()
    {
        return m_Ragdoll;
    }

    const RagdollRuntime& GetRagdoll() const
    {
        return m_Ragdoll;
    }

    RagdollConstraintSolver& GetConstraintSolver()
    {
        return m_ConstraintSolver;
    }

    const RagdollConstraintSolver& GetConstraintSolver() const
    {
        return m_ConstraintSolver;
    }

    RagdollPhysicsBridge& GetPhysicsBridge()
    {
        return m_PhysicsBridge;
    }

    const RagdollPhysicsBridge& GetPhysicsBridge() const
    {
        return m_PhysicsBridge;
    }

private:
    // Ragdollを適用するglTF Skinです。同一Skinを使う全Runtime PrimitiveへPoseを配布します。
    std::size_t m_SkinIndex = 0u;

    // SkinnedMeshRuntimeAssetの所有権は持ちません。Attach先Assetより長生きしないことが前提です。
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;

    // Skeleton <-> Ragdoll Body Pose変換を担当します。
    RagdollRuntime m_Ragdoll{};

    // Parent / Child Joint接続とSwing / Twist制限を担当します。
    RagdollConstraintSolver m_ConstraintSolver{};

    // Scene ECS上のRigidBody / Collider Entity生成とBody State同期を担当します。
    RagdollPhysicsBridge m_PhysicsBridge{};

    // trueなら直前のAnimation評価後PoseがSampleAnimationMotion()でRagdollRuntimeへ同期済みです。
    // EnterRagdoll()はこのStateを見て、計測済みVelocityを0で上書きしないようにします。
    bool m_HasAnimationMotionSample = false;
};

} // namespace Gltf
} // namespace Raven
