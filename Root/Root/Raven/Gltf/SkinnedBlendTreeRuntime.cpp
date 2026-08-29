// Raven/Gltf/SkinnedBlendTreeRuntime.cpp
#include "Raven/Gltf/SkinnedBlendTreeRuntime.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Animation/SkeletalMeshDeformer.h"
#include "Raven/Gltf/AnimationImporter.h"
#include "Raven/Gltf/SkinImporter.h"
#include "Raven/Gltf/SkinnedMeshRuntime.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"

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

SkeletalMeshDeformer* GetSkeletalDeformer(
    const RuntimeSkinnedPrimitive& primitive,
    std::string* errorMessage)
{
    if (primitive.DeformationInstance == nullptr)
    {
        SetError(errorMessage, "BlendTree対象のMeshDeformationInstanceがnullptrです");
        return nullptr;
    }

    MeshDeformer* baseDeformer = primitive.DeformationInstance->GetDeformer();
    if (baseDeformer == nullptr)
    {
        SetError(errorMessage, "BlendTree対象のMeshDeformerがnullptrです");
        return nullptr;
    }

    SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
    if (skeletalDeformer == nullptr)
    {
        SetError(errorMessage, "BlendTree対象のDeformerがSkeletalMeshDeformerではありません");
        return nullptr;
    }

    return skeletalDeformer;
}

bool SkeletonsMatch(
    const Skeleton& lhs,
    const Skeleton& rhs,
    std::string* errorMessage)
{
    if (lhs.GetBoneCount() != rhs.GetBoneCount())
    {
        return SetError(errorMessage, "BlendTree SkeletonのBone数が一致しません");
    }

    for (std::size_t boneIndex = 0u; boneIndex < lhs.GetBoneCount(); ++boneIndex)
    {
        const BoneIndex index = static_cast<BoneIndex>(boneIndex);
        const Bone& leftBone = lhs.GetBone(index);
        const Bone& rightBone = rhs.GetBone(index);

        if (leftBone.Name != rightBone.Name || leftBone.Parent != rightBone.Parent)
        {
            return SetError(
                errorMessage,
                "BlendTree SkeletonのBone順序/階層が一致しません。bone="
                    + std::to_string(boneIndex));
        }
    }

    return true;
}

} // namespace

SkinnedBlendTreeRuntime::SkinState* SkinnedBlendTreeRuntime::FindSkinState(
    std::size_t skinIndex)
{
    const auto it = std::find_if(
        m_SkinStates.begin(),
        m_SkinStates.end(),
        [skinIndex](const SkinState& state)
        {
            return state.SkinIndex == skinIndex;
        });

    if (it == m_SkinStates.end())
    {
        return nullptr;
    }

    return &(*it);
}

const SkinnedBlendTreeRuntime::SkinState* SkinnedBlendTreeRuntime::FindSkinState(
    std::size_t skinIndex) const
{
    const auto it = std::find_if(
        m_SkinStates.begin(),
        m_SkinStates.end(),
        [skinIndex](const SkinState& state)
        {
            return state.SkinIndex == skinIndex;
        });

    if (it == m_SkinStates.end())
    {
        return nullptr;
    }

    return &(*it);
}

const SkinnedBlendTreeRuntime::RuntimeClip* SkinnedBlendTreeRuntime::FindClip(
    const SkinState& state,
    const std::string& animationName) const
{
    const auto it = std::find_if(
        state.Clips.begin(),
        state.Clips.end(),
        [&animationName](const RuntimeClip& clip)
        {
            return clip.Name == animationName;
        });

    if (it == state.Clips.end())
    {
        return nullptr;
    }

    return &(*it);
}

bool SkinnedBlendTreeRuntime::AttachFromGlb(
    const std::string& filePath,
    SkinnedMeshRuntimeAsset& targetAsset,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const std::vector<RuntimeSkinnedPrimitive>& primitives = targetAsset.GetPrimitives();
    if (primitives.empty())
    {
        return SetError(errorMessage, "BlendTreeを接続するRuntime Skinned Primitiveがありません");
    }

    std::vector<ImportedSkin> importedSkins;
    if (SkinImporter::LoadFromGlb(filePath, importedSkins, errorMessage) == false)
    {
        return false;
    }

    std::vector<SkinState> states;
    states.reserve(importedSkins.size());

    for (const RuntimeSkinnedPrimitive& primitive : primitives)
    {
        const auto existing = std::find_if(
            states.begin(),
            states.end(),
            [&primitive](const SkinState& state)
            {
                return state.SkinIndex == primitive.SkinIndex;
            });

        if (existing != states.end())
        {
            continue;
        }

        if (primitive.SkinIndex >= importedSkins.size())
        {
            return SetError(errorMessage, "Runtime PrimitiveのSkinIndexがGLB skins[]範囲外です");
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        const ImportedSkin& importedSkin = importedSkins[primitive.SkinIndex];
        if (SkeletonsMatch(importedSkin.SkeletonData, deformer->GetSkeleton(), errorMessage) == false)
        {
            return false;
        }

        std::vector<ImportedAnimationClip> importedClips;
        if (AnimationImporter::LoadFromGlb(
                filePath,
                importedSkin,
                importedClips,
                errorMessage) == false)
        {
            return false;
        }

        SkinState state{};
        state.SkinIndex = primitive.SkinIndex;
        state.AnimatorInstance.SetSkeleton(&deformer->GetSkeleton());
        state.AnimatorInstance.SetLoop(true);

        state.Clips.reserve(importedClips.size());
        for (ImportedAnimationClip& importedClip : importedClips)
        {
            RuntimeClip runtimeClip{};
            runtimeClip.SourceAnimationIndex = importedClip.AnimationIndex;
            runtimeClip.Name = std::move(importedClip.Name);
            runtimeClip.Clip = std::make_shared<AnimationClip>(std::move(importedClip.Clip));

            if (runtimeClip.Clip == nullptr)
            {
                return SetError(errorMessage, "BlendTree用AnimationClipの生成に失敗しました");
            }

            state.Clips.emplace_back(std::move(runtimeClip));
        }

        states.emplace_back(std::move(state));
    }

    if (states.empty())
    {
        return SetError(errorMessage, "BlendTreeを接続できるSkinがありません");
    }

    m_TargetAsset = &targetAsset;
    m_SkinStates = std::move(states);
    return true;
}

bool SkinnedBlendTreeRuntime::GetAnimationNames(
    std::size_t skinIndex,
    std::vector<std::string>& outNames,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    outNames.clear();

    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }

    // RuntimeClip自体を外部へ公開せず、診断とAnimation名解決に必要な名前だけをSnapshotとして返します。
    // これによりClip所有権やAnimatorの再生状態をCharacter側へ漏らさず、Runtimeの責務境界を維持します。
    outNames.reserve(state->Clips.size());
    for (const RuntimeClip& clip : state->Clips)
    {
        outNames.push_back(clip.Name);
    }

    return true;
}

bool SkinnedBlendTreeRuntime::Configure(
    std::size_t skinIndex,
    const LocomotionBlendTreeConfig& config,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }

    if (config.IdleAnimationName.empty()
        || config.WalkAnimationName.empty()
        || config.RunAnimationName.empty())
    {
        return SetError(errorMessage, "Idle / Walk / Run Animation名は空にできません");
    }

    if (std::isfinite(config.IdleThreshold) == false
        || std::isfinite(config.WalkThreshold) == false
        || std::isfinite(config.RunThreshold) == false
        || config.IdleThreshold < 0.0f
        || config.WalkThreshold <= config.IdleThreshold
        || config.RunThreshold <= config.WalkThreshold)
    {
        return SetError(
            errorMessage,
            "BlendTree Thresholdは 0 <= Idle < Walk < Run を満たす必要があります");
    }

    if (std::isfinite(config.WalkAuthoredMotionSpeed) == false
        || std::isfinite(config.RunAuthoredMotionSpeed) == false
        || std::isfinite(config.MinLocomotionPlaybackSpeed) == false
        || std::isfinite(config.MaxLocomotionPlaybackSpeed) == false
        || config.WalkAuthoredMotionSpeed <= 0.0f
        || config.RunAuthoredMotionSpeed <= config.WalkAuthoredMotionSpeed
        || config.MinLocomotionPlaybackSpeed <= 0.0f
        || config.MaxLocomotionPlaybackSpeed < config.MinLocomotionPlaybackSpeed)
    {
        return SetError(
            errorMessage,
            "Locomotion補正値は 0 < WalkAuthored < RunAuthored かつ 0 < MinPlayback <= MaxPlayback を満たす必要があります");
    }

    const RuntimeClip* idleClip = FindClip(*state, config.IdleAnimationName);
    const RuntimeClip* walkClip = FindClip(*state, config.WalkAnimationName);
    const RuntimeClip* runClip = FindClip(*state, config.RunAnimationName);

    if (idleClip == nullptr)
    {
        return SetError(errorMessage, "Idle Animationが見つかりません: " + config.IdleAnimationName);
    }
    if (walkClip == nullptr)
    {
        return SetError(errorMessage, "Walk Animationが見つかりません: " + config.WalkAnimationName);
    }
    if (runClip == nullptr)
    {
        return SetError(errorMessage, "Run Animationが見つかりません: " + config.RunAnimationName);
    }
    if (idleClip->Clip == nullptr || walkClip->Clip == nullptr || runClip->Clip == nullptr)
    {
        return SetError(errorMessage, "BlendTree用AnimationClipにnullptrが含まれています");
    }

    // ========================================================================
    // 1D BlendTree構築
    // ========================================================================
    // Configure中は一時Treeへ構築し、3 Childすべて成功した場合だけRuntime Stateへ反映します。
    // AddChild()はThreshold重複も拒否するため、壊れたTreeを途中状態で公開しません。
    std::shared_ptr<BlendTree1D> blendTree = std::make_shared<BlendTree1D>();
    if (blendTree == nullptr)
    {
        return SetError(errorMessage, "BlendTree1Dの生成に失敗しました");
    }

    if (blendTree->AddChild(config.IdleThreshold, idleClip->Clip) == false
        || blendTree->AddChild(config.WalkThreshold, walkClip->Clip) == false
        || blendTree->AddChild(config.RunThreshold, runClip->Clip) == false)
    {
        return SetError(errorMessage, "BlendTree1D Childの登録に失敗しました");
    }

    state->LocomotionTree = std::move(blendTree);
    state->MovementSpeed = config.IdleThreshold;
    state->WalkAuthoredMotionSpeed = config.WalkAuthoredMotionSpeed;
    state->RunAuthoredMotionSpeed = config.RunAuthoredMotionSpeed;
    state->MinLocomotionPlaybackSpeed = config.MinLocomotionPlaybackSpeed;
    state->MaxLocomotionPlaybackSpeed = config.MaxLocomotionPlaybackSpeed;
    state->ReferenceMotionSpeed = 0.0f;
    state->LocomotionPlaybackSpeed = 1.0f;
    state->Configured = true;

    // 初回だけPlayBlendTree()し、以後はParameterだけ更新します。
    // restart=trueでIdle側の位相0から開始します。
    state->AnimatorInstance.PlayBlendTree(
        state->LocomotionTree,
        state->MovementSpeed,
        true);
    state->AnimatorInstance.SetSpeed(1.0f);

    return true;
}

bool SkinnedBlendTreeRuntime::UpdateLocomotionPlaybackSpeed(
    SkinState& state,
    std::string* errorMessage)
{
    if (state.Configured == false || state.LocomotionTree == nullptr)
    {
        return SetError(errorMessage, "Locomotion Playback補正対象のBlendTreeがConfigureされていません");
    }

    BlendTree1DDebugInfo blendInfo{};
    if (state.LocomotionTree->GetDebugInfo(state.MovementSpeed, blendInfo) == false)
    {
        return SetError(errorMessage, "Locomotion Playback補正用Blend Weightの取得に失敗しました");
    }

    // ========================================================================
    // Blend Weight -> Reference Motion Speed
    // ========================================================================
    // BlendTree ChildはConfigure時に Idle / Walk / Run のThreshold昇順で登録されています。
    // Pose Blendと同じWeightでClip側の想定移動速度も補間することで、Walk->Run境界でも
    // Playback Speedが不連続に切り替わらず、足運びの位相を維持したまま補正できます。
    const auto resolveAuthoredMotionSpeed = [&state](std::size_t childIndex)
    {
        if (childIndex == 0u)
        {
            return 0.0f;
        }
        if (childIndex == 1u)
        {
            return state.WalkAuthoredMotionSpeed;
        }
        if (childIndex == 2u)
        {
            return state.RunAuthoredMotionSpeed;
        }

        return 0.0f;
    };

    const float leftReferenceSpeed = resolveAuthoredMotionSpeed(blendInfo.LeftChildIndex);
    const float rightReferenceSpeed = resolveAuthoredMotionSpeed(blendInfo.RightChildIndex);
    state.ReferenceMotionSpeed =
        leftReferenceSpeed * blendInfo.LeftWeight
        + rightReferenceSpeed * blendInfo.RightWeight;

    // 完全IdleではReference Speedが0になります。
    // ここで0除算を避けるだけでなくIdle Clip本来の呼吸等を止めないよう1.0倍速を維持します。
    constexpr float ReferenceSpeedEpsilon = 1.0e-5f;
    if (state.ReferenceMotionSpeed <= ReferenceSpeedEpsilon)
    {
        state.LocomotionPlaybackSpeed = 1.0f;
    }
    else
    {
        const float requestedPlaybackSpeed = state.MovementSpeed / state.ReferenceMotionSpeed;
        state.LocomotionPlaybackSpeed = std::clamp(
            requestedPlaybackSpeed,
            state.MinLocomotionPlaybackSpeed,
            state.MaxLocomotionPlaybackSpeed);
    }

    state.AnimatorInstance.SetSpeed(state.LocomotionPlaybackSpeed);
    return true;
}

bool SkinnedBlendTreeRuntime::SetMovementSpeed(
    std::size_t skinIndex,
    float movementSpeed,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(movementSpeed) == false || movementSpeed < 0.0f)
    {
        return SetError(errorMessage, "Movement Speedは0以上の有限値である必要があります");
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }
    if (state->Configured == false || state->LocomotionTree == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTreeがConfigureされていません");
    }

    state->MovementSpeed = movementSpeed;

    // ========================================================================
    // Speed変更ではAnimation位相をリスタートしない
    // ========================================================================
    // BlendTreeの利点は、速度が連続変化してもAnimatorのNormalizedTimeを維持したまま
    // PoseだけをIdle -> Walk -> Runへ連続変化させられる点です。
    // 毎Frame PlayBlendTree()すると歩行周期が0へ戻るため、Parameter更新専用APIを使います。
    if (state->AnimatorInstance.SetCurrentBlendParameter(movementSpeed) == false)
    {
        return SetError(errorMessage, "Animator BlendTree Parameterの更新に失敗しました");
    }

    // Parameterと同じFrameのBlend Weightを使ってPlayback Speedも更新します。
    // これによりCharacterの実速度と足運び速度の差を補正しつつ、NormalizedTime自体は連続したままです。
    return UpdateLocomotionPlaybackSpeed(*state, errorMessage);
}

bool SkinnedBlendTreeRuntime::SetPlaybackSpeed(
    std::size_t skinIndex,
    float playbackSpeed,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(playbackSpeed) == false)
    {
        return SetError(errorMessage, "Playback Speedは有限値である必要があります");
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }

    state->AnimatorInstance.SetSpeed(playbackSpeed);
    return true;
}

bool SkinnedBlendTreeRuntime::GetDebugInfo(
    std::size_t skinIndex,
    BlendTree1DDebugInfo& outInfo,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }
    if (state->Configured == false || state->LocomotionTree == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTreeがConfigureされていません");
    }

    if (state->LocomotionTree->GetDebugInfo(state->MovementSpeed, outInfo) == false)
    {
        return SetError(errorMessage, "BlendTree DebugInfoの取得に失敗しました");
    }

    return true;
}

bool SkinnedBlendTreeRuntime::GetLocomotionPlaybackDebugInfo(
    std::size_t skinIndex,
    LocomotionPlaybackDebugInfo& outInfo,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTree Runtime Stateがありません");
    }
    if (state->Configured == false || state->LocomotionTree == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのBlendTreeがConfigureされていません");
    }

    outInfo.MovementSpeed = state->MovementSpeed;
    outInfo.ReferenceMotionSpeed = state->ReferenceMotionSpeed;
    outInfo.PlaybackSpeed = state->LocomotionPlaybackSpeed;
    return true;
}

bool SkinnedBlendTreeRuntime::ApplyPoseToSkin(
    const SkinState& state,
    std::string* errorMessage)
{
    if (m_TargetAsset == nullptr)
    {
        return SetError(errorMessage, "BlendTree RuntimeがSkinnedMeshRuntimeAssetへAttachされていません");
    }

    const Skeleton* animatorSkeleton = state.AnimatorInstance.GetSkeleton();
    if (animatorSkeleton == nullptr)
    {
        return SetError(errorMessage, "BlendTree Animator Skeletonがnullptrです");
    }

    const SkeletonPose& pose = state.AnimatorInstance.GetCurrentSkeletonPose();
    if (pose.GetBoneCount() != animatorSkeleton->GetBoneCount())
    {
        return SetError(errorMessage, "BlendTree評価済みPoseのBone数がSkeletonと一致しません");
    }

    bool found = false;
    for (const RuntimeSkinnedPrimitive& primitive : m_TargetAsset->GetPrimitives())
    {
        if (primitive.SkinIndex != state.SkinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        if (SkeletonsMatch(*animatorSkeleton, deformer->GetSkeleton(), errorMessage) == false)
        {
            return false;
        }

        // 同じSkinを使うBody / Clothesへ、Skinごとに1回だけ評価した同一Poseを配布します。
        deformer->GetPose() = pose;
        found = true;
    }

    if (found == false)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    return true;
}

bool SkinnedBlendTreeRuntime::Update(
    float deltaTime,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_TargetAsset == nullptr)
    {
        return SetError(errorMessage, "BlendTree RuntimeがSkinnedMeshRuntimeAssetへAttachされていません");
    }
    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    for (SkinState& state : m_SkinStates)
    {
        if (state.Configured == false)
        {
            continue;
        }

        state.AnimatorInstance.Update(deltaTime);

        if (ApplyPoseToSkin(state, errorMessage) == false)
        {
            return false;
        }
    }

    // Pose配布後は既存のCPU Skinning -> Mesh::SyncGeometry()経路をそのまま利用します。
    return m_TargetAsset->Update(deltaTime, errorMessage);
}

} // namespace Gltf
} // namespace Raven
