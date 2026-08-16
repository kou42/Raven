// Raven/Gltf/SkinnedRagdollRuntime.cpp
#include "Raven/Gltf/SkinnedRagdollRuntime.h"

#include <cmath>
#include <string>
#include <utility>

#include "Raven/Animation/SkeletalMeshDeformer.h"
#include "Raven/Gltf/SkinnedMeshRuntime.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace Gltf
{
namespace
{

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

// RuntimeSkinnedPrimitiveが持つ汎用MeshDeformerからSkeletalMeshDeformerを取得します。
// RagdollはSkeletonPoseへ書き戻す必要があるため、単なるMeshDeformerでは処理できません。
SkeletalMeshDeformer* GetSkeletalDeformer(
    const RuntimeSkinnedPrimitive& primitive,
    std::string* errorMessage)
{
    if (primitive.DeformationInstance == nullptr)
    {
        SetError(errorMessage, "Ragdoll対象のMeshDeformationInstanceがnullptrです");
        return nullptr;
    }

    MeshDeformer* baseDeformer = primitive.DeformationInstance->GetDeformer();
    if (baseDeformer == nullptr)
    {
        SetError(errorMessage, "Ragdoll対象のMeshDeformerがnullptrです");
        return nullptr;
    }

    SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
    if (skeletalDeformer == nullptr)
    {
        SetError(errorMessage, "Ragdoll対象DeformerがSkeletalMeshDeformerではありません");
        return nullptr;
    }

    return skeletalDeformer;
}

// 同一SkinIndexを共有するPrimitiveへSkeletonPoseをそのままコピーするための構造検証です。
// Bone数だけでなくName / Parentも比較し、同じBoneIndexが別のBoneを指すケースを防ぎます。
bool SkeletonsMatch(const Skeleton& lhs, const Skeleton& rhs)
{
    if (lhs.GetBoneCount() != rhs.GetBoneCount())
    {
        return false;
    }

    for (std::size_t boneSlot = 0u; boneSlot < lhs.GetBoneCount(); ++boneSlot)
    {
        const BoneIndex boneIndex = static_cast<BoneIndex>(boneSlot);
        const Bone& leftBone = lhs.GetBone(boneIndex);
        const Bone& rightBone = rhs.GetBone(boneIndex);

        if (leftBone.Name != rightBone.Name || leftBone.Parent != rightBone.Parent)
        {
            return false;
        }
    }

    return true;
}

} // namespace

bool SkinnedRagdollRuntime::Attach(
    std::size_t skinIndex,
    SkinnedMeshRuntimeAsset& targetAsset,
    const RagdollDefinition& definition,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    // Physics EntityはScene側に実体を持つため、Scene参照なしでAttachをやり直すと孤児Entityを残します。
    // 再Attachする場合は先にDestroyPhysicsBodies()を明示的に呼ぶ契約にします。
    if (m_PhysicsBridge.IsCreated())
    {
        return SetError(errorMessage, "Physics Body生成中は再Attachできません。先にDestroyPhysicsBodies()を呼んでください");
    }

    SkeletalMeshDeformer* referenceDeformer = nullptr;

    // 指定Skinを利用している全Primitiveを確認します。
    // Body / Clothesなどが同一Skinを共有している場合、最初のDeformerを基準Skeletonとして使用し、
    // 2つ目以降が同じBone Index契約を持っていることをAttach時点で保証します。
    for (const RuntimeSkinnedPrimitive& primitive : targetAsset.GetPrimitives())
    {
        if (primitive.SkinIndex != skinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        if (referenceDeformer == nullptr)
        {
            referenceDeformer = deformer;
            continue;
        }

        // 同一Skinを共有するBody / Clothesは同じBone Index契約でPoseをコピーするため、
        // Attach時点でSkeleton構造が一致していることを確認します。
        if (SkeletonsMatch(referenceDeformer->GetSkeleton(), deformer->GetSkeleton()) == false)
        {
            return SetError(errorMessage, "同一SkinIndexのRuntime Primitive間でSkeletonが一致しません");
        }
    }

    if (referenceDeformer == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    // RagdollRuntimeはglTF Runtime Assetを直接参照せず、SkeletonとDefinitionだけから構築します。
    // これによりRagdollのPose変換ロジックをglTF Import処理から独立させています。
    RagdollRuntime ragdoll;
    if (ragdoll.Build(referenceDeformer->GetSkeleton(), definition, errorMessage) == false)
    {
        return false;
    }

    m_SkinIndex = skinIndex;
    m_TargetAsset = &targetAsset;
    m_Ragdoll = std::move(ragdoll);

    // Attachし直したRagdollへ古いConstraint Rest PoseやPhysics Entity対応を持ち越さないよう、
    // Solver / Bridgeは新しい空状態へ戻します。
    m_ConstraintSolver = RagdollConstraintSolver{};
    m_PhysicsBridge = RagdollPhysicsBridge{};
    return true;
}

bool SkinnedRagdollRuntime::EnterRagdoll(std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_TargetAsset == nullptr || m_Ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "SkinnedRagdollRuntimeがAttachされていません");
    }

    // 同じSkinを使うPrimitiveはAnimation Runtime側で同じPoseへ同期されるため、
    // 最初の1つをRagdoll開始Poseの基準として使用します。
    //
    // ここで現在Animation Poseを取り込むことで、Ragdoll開始時にBind Poseや原点から
    // Physics Bodyが生成されてMeshが跳ぶことを防ぎます。
    for (const RuntimeSkinnedPrimitive& primitive : m_TargetAsset->GetPrimitives())
    {
        if (primitive.SkinIndex != m_SkinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        return m_Ragdoll.CaptureAnimationPose(deformer->GetPose(), errorMessage);
    }

    return SetError(errorMessage, "Ragdoll開始Poseを取得できるPrimitiveがありません");
}

bool SkinnedRagdollRuntime::InitializeConstraints(std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_Ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "Constraint初期化にはBuild済みRagdollが必要です");
    }

    // EnterRagdoll()後のBody PoseをRest Poseとして保存します。
    // AnimationからRagdollへ切り替えた瞬間に関節がConstraint中心へ跳ぶことを防ぎます。
    // そのため通常は EnterRagdoll() -> InitializeConstraints() の順で呼びます。
    return m_ConstraintSolver.Initialize(m_Ragdoll, errorMessage);
}

bool SkinnedRagdollRuntime::CreatePhysicsBodies(
    Scene& scene,
    const std::string& entityNamePrefix,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_Ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "Physics Body生成にはAttach済みRagdollが必要です");
    }

    // CreateBodies()が参照するBody PoseはEnterRagdoll()でAnimationから取得済みであることを想定します。
    // 呼び出し順を明確に保ち、Body生成時に勝手にBind Poseへ戻す処理は入れません。
    //
    // SceneのPhysicsWorldはECS上のRigidBodyComponent / ColliderComponentを直接処理するため、
    // BridgeがBoneごとのEntityを生成した時点で通常のPhysics Step対象になります。
    return m_PhysicsBridge.CreateBodies(
        scene,
        m_Ragdoll,
        entityNamePrefix,
        errorMessage);
}

void SkinnedRagdollRuntime::DestroyPhysicsBodies(Scene& scene)
{
    // Physics Entityの所有先はSceneです。
    // Runtime側でEntityを直接破棄せずBridgeへ委譲し、生成時のBone <-> Entity対応も同時に破棄します。
    m_PhysicsBridge.DestroyBodies(scene);
}

bool SkinnedRagdollRuntime::SetBodyState(
    BoneIndex boneIndex,
    const RagdollBodyState& state,
    std::string* errorMessage)
{
    // PhysicsWorldを経由しないテストや、別Physics BackendからのPose入力にも利用できるよう、
    // RagdollRuntimeの低レベル更新APIをそのまま公開しています。
    return m_Ragdoll.SetBodyState(boneIndex, state, errorMessage);
}

bool SkinnedRagdollRuntime::SetBodyState(
    const std::string& boneName,
    const RagdollBodyState& state,
    std::string* errorMessage)
{
    return m_Ragdoll.SetBodyState(boneName, state, errorMessage);
}

bool SkinnedRagdollRuntime::SolveConstraints(
    const RagdollConstraintSolverSettings& settings,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_ConstraintSolver.IsInitialized() == false)
    {
        return SetError(errorMessage, "Ragdoll Constraint SolverがInitializeされていません");
    }

    // Physics積分後に離れたParent / Child AnchorをBall/Socket制約で戻し、
    // その後Swing / Twist角度制限を反復して人体として許容する姿勢へ投影します。
    return m_ConstraintSolver.Solve(m_Ragdoll, settings, errorMessage);
}

bool SkinnedRagdollRuntime::SyncPhysicsToRagdoll(
    const Scene& scene,
    std::string* errorMessage)
{
    // PhysicsWorldが更新したTransform / RigidBodyの結果をRagdollBodyStateへ取り込みます。
    // Joint ConstraintとSkeleton変換はScene Componentへ直接依存せず、RagdollBodyStateを境界にします。
    return m_PhysicsBridge.SyncPhysicsToRagdoll(
        scene,
        m_Ragdoll,
        errorMessage);
}

bool SkinnedRagdollRuntime::SyncRagdollToPhysics(
    Scene& scene,
    bool syncVelocities,
    std::string* errorMessage)
{
    // Constraint Solverで補正したBody PoseをScene Entityへ戻します。
    // Projection Solver使用中は通常syncVelocities=falseとして、Contact Solverが生成した速度を維持します。
    return m_PhysicsBridge.SyncRagdollToPhysics(
        scene,
        m_Ragdoll,
        syncVelocities,
        errorMessage);
}

bool SkinnedRagdollRuntime::ApplyPose(
    float weight,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_TargetAsset == nullptr || m_Ragdoll.IsBuilt() == false)
    {
        return SetError(errorMessage, "SkinnedRagdollRuntimeがAttachされていません");
    }
    if (std::isfinite(weight) == false)
    {
        return SetError(errorMessage, "Ragdoll Blend Weightは有限値である必要があります");
    }

    SkeletonPose outputPose;
    bool poseCreated = false;
    const Skeleton* referenceSkeleton = nullptr;

    // ========================================================================
    // Animation PoseをBlend元としてRagdoll Poseを構築
    // ========================================================================
    // weight < 1では「現在Animation Pose」とPhysics Poseを混ぜます。
    // そのため最初のPrimitiveの現在PoseをコピーしてからRagdollRuntimeへ渡します。
    // weight=1の場合も同じ経路を通すことで、完全Ragdollと復帰BlendでPose生成手順を統一します。
    for (const RuntimeSkinnedPrimitive& primitive : m_TargetAsset->GetPrimitives())
    {
        if (primitive.SkinIndex != m_SkinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        outputPose = deformer->GetPose();
        referenceSkeleton = &deformer->GetSkeleton();
        poseCreated = true;
        break;
    }

    if (poseCreated == false || referenceSkeleton == nullptr)
    {
        return SetError(errorMessage, "Ragdoll PoseのBlend元を取得できません");
    }

    // RagdollRuntime内部でGlobal Body PoseをBone Local TRSへ戻し、
    // weightに応じてTranslation=Lerp / Rotation=Slerp / Scale=LerpでBlendします。
    if (m_Ragdoll.ApplyToSkeletonPose(outputPose, weight, errorMessage) == false)
    {
        return false;
    }

    // 完成Poseを同じSkinを使う全Primitiveへ配布します。
    // Body / Clothesを別々にRagdoll変換しないことでPose差を防ぎます。
    for (const RuntimeSkinnedPrimitive& primitive : m_TargetAsset->GetPrimitives())
    {
        if (primitive.SkinIndex != m_SkinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        if (SkeletonsMatch(*referenceSkeleton, deformer->GetSkeleton()) == false)
        {
            return SetError(errorMessage, "Ragdoll Pose配布先Skeletonが基準Skeletonと一致しません");
        }

        deformer->GetPose() = outputPose;
    }

    return true;
}

bool SkinnedRagdollRuntime::UpdatePhysicsDrivenMesh(
    Scene& scene,
    float deltaTime,
    const RagdollConstraintSolverSettings& settings,
    float weight,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    // ========================================================================
    // PhysicsWorld -> Ragdoll -> Constraint -> PhysicsWorld -> Skeleton
    // ========================================================================
    // Scene::OnUpdatePhysics()が固定Stepで重力・Collision・Contact Solverを実行した後に呼びます。
    // Joint Constraintは現在RagdollBodyState上のProjection Solverなので、結果をPhysics Entityへ
    // 戻して次の固定Step開始状態とSkeleton表示状態を一致させることが重要です。
    if (SyncPhysicsToRagdoll(scene, errorMessage) == false)
    {
        return false;
    }

    // Physicsの衝突・重力で更新されたBodyを、Joint接続と可動域を満たすPoseへ補正します。
    if (SolveConstraints(settings, errorMessage) == false)
    {
        return false;
    }

    // ConstraintはPosition / Orientationを補正しますが、現在はAngular Impulse Solverではありません。
    // Physicsの衝突で得た速度は維持したいのでsyncVelocities=falseで姿勢だけ戻します。
    if (SyncRagdollToPhysics(scene, false, errorMessage) == false)
    {
        return false;
    }

    // Constraint済みBody PoseをSkeletonへ変換し、同じSkinを使う全Primitiveへ配布します。
    if (ApplyPose(weight, errorMessage) == false)
    {
        return false;
    }

    // 最後に既存SkinnedMeshRuntimeの更新経路を使い、CPU SkinningとGPU Geometry同期まで進めます。
    return m_TargetAsset->Update(deltaTime, errorMessage);
}

bool SkinnedRagdollRuntime::UpdateConstrainedMesh(
    float deltaTime,
    const RagdollConstraintSolverSettings& settings,
    float weight,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    // PhysicsWorldを介さない経路です。
    // 外部からSetBodyState()したPoseやConstraint単体テストに対して制約を解き、Meshまで更新します。
    if (SolveConstraints(settings, errorMessage) == false)
    {
        return false;
    }

    if (ApplyPose(weight, errorMessage) == false)
    {
        return false;
    }

    return m_TargetAsset->Update(deltaTime, errorMessage);
}

bool SkinnedRagdollRuntime::UpdateMesh(
    float deltaTime,
    float weight,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    // Constraintを使わない最小経路です。
    // RagdollBodyState -> SkeletonPose変換だけを確認したい場合にも利用できます。
    if (ApplyPose(weight, errorMessage) == false)
    {
        return false;
    }

    return m_TargetAsset->Update(deltaTime, errorMessage);
}

} // namespace Gltf
} // namespace Raven
