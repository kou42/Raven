// Raven/Gltf/SkinnedAnimationRuntime.cpp
#include "Raven/Gltf/SkinnedAnimationRuntime.h"

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
        SetError(errorMessage, "Animation対象のMeshDeformationInstanceがnullptrです");
        return nullptr;
    }

    MeshDeformer* baseDeformer = primitive.DeformationInstance->GetDeformer();
    if (baseDeformer == nullptr)
    {
        SetError(errorMessage, "Animation対象のMeshDeformerがnullptrです");
        return nullptr;
    }

    SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
    if (skeletalDeformer == nullptr)
    {
        SetError(errorMessage, "Animation対象のDeformerがSkeletalMeshDeformerではありません");
        return nullptr;
    }

    return skeletalDeformer;
}

bool SkeletonsMatch(
    const Skeleton& importedSkeleton,
    const Skeleton& runtimeSkeleton,
    std::string* errorMessage)
{
    if (importedSkeleton.GetBoneCount() != runtimeSkeleton.GetBoneCount())
    {
        return SetError(errorMessage, "Animation Import時SkeletonとRuntime SkeletonのBone数が一致しません");
    }

    // AnimationClipはBoneIndexを直接保持するため、Bone数だけでなくIndexごとの名前とParentも
    // 一致している必要があります。ここを曖昧にすると別Skeleton向けClipを偶然Sampleできても、
    // 全く別のBoneへTransformを書き込んでしまうためAttach時点で厳密に検証します。
    for (std::size_t boneIndex = 0u; boneIndex < importedSkeleton.GetBoneCount(); ++boneIndex)
    {
        const BoneIndex index = static_cast<BoneIndex>(boneIndex);
        const Bone& importedBone = importedSkeleton.GetBone(index);
        const Bone& runtimeBone = runtimeSkeleton.GetBone(index);

        if (importedBone.Name != runtimeBone.Name || importedBone.Parent != runtimeBone.Parent)
        {
            return SetError(
                errorMessage,
                "Animation Import時SkeletonとRuntime SkeletonのBone順序/階層が一致しません。bone="
                    + std::to_string(boneIndex));
        }
    }

    return true;
}

} // namespace

SkinnedAnimationRuntime::SkinAnimationState* SkinnedAnimationRuntime::FindSkinState(
    std::size_t skinIndex)
{
    const auto it = std::find_if(
        m_SkinStates.begin(),
        m_SkinStates.end(),
        [skinIndex](const SkinAnimationState& state)
        {
            return state.SkinIndex == skinIndex;
        });

    if (it == m_SkinStates.end())
    {
        return nullptr;
    }

    return &(*it);
}

const SkinnedAnimationRuntime::SkinAnimationState* SkinnedAnimationRuntime::FindSkinState(
    std::size_t skinIndex) const
{
    const auto it = std::find_if(
        m_SkinStates.begin(),
        m_SkinStates.end(),
        [skinIndex](const SkinAnimationState& state)
        {
            return state.SkinIndex == skinIndex;
        });

    if (it == m_SkinStates.end())
    {
        return nullptr;
    }

    return &(*it);
}

bool SkinnedAnimationRuntime::AttachFromGlb(
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
        return SetError(errorMessage, "Animationを接続するRuntime Skinned Primitiveがありません");
    }

    std::vector<ImportedSkin> importedSkins;
    if (SkinImporter::LoadFromGlb(filePath, importedSkins, errorMessage) == false)
    {
        return false;
    }

    std::vector<SkinAnimationState> states;
    states.reserve(importedSkins.size());

    // Runtime側で実際に使用されているSkinだけAnimatorを作ります。
    // glTFに未使用Skinが含まれていても、描画対象が無ければPose配布先も無いためRuntime Stateは不要です。
    for (const RuntimeSkinnedPrimitive& primitive : primitives)
    {
        const auto existing = std::find_if(
            states.begin(),
            states.end(),
            [&primitive](const SkinAnimationState& state)
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

        SkinAnimationState state{};
        state.SkinIndex = primitive.SkinIndex;

        // AnimatorはRuntime Deformerが所有するSkeleton定義を参照します。
        // ImportedSkin側の一時Skeletonを参照するとAttachFromGlb()終了後にdangling pointerになるため、
        // 実際にSkinningで使われるDeformer側Skeletonを接続することが重要です。
        state.AnimatorInstance.SetSkeleton(&deformer->GetSkeleton());

        state.Clips.reserve(importedClips.size());
        for (ImportedAnimationClip& importedClip : importedClips)
        {
            RuntimeAnimationClip runtimeClip{};
            runtimeClip.SourceAnimationIndex = importedClip.AnimationIndex;
            runtimeClip.Name = std::move(importedClip.Name);
            runtimeClip.Clip = std::make_shared<AnimationClip>(std::move(importedClip.Clip));

            if (runtimeClip.Clip == nullptr)
            {
                return SetError(errorMessage, "AnimationClip Runtime Instanceの生成に失敗しました");
            }

            state.Clips.emplace_back(std::move(runtimeClip));
        }

        states.emplace_back(std::move(state));
    }

    if (states.empty())
    {
        return SetError(errorMessage, "Animationを接続できるSkinがありません");
    }

    // 全SkinのImportと検証が成功してから現在Stateを差し替えます。
    // 途中失敗時に一部Skinだけ新しいAnimationへ切り替わる半端な状態を残しません。
    m_TargetAsset = &targetAsset;
    m_SkinStates = std::move(states);
    return true;
}

bool SkinnedAnimationRuntime::Play(
    std::size_t skinIndex,
    const std::string& animationName,
    bool restart,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    const auto it = std::find_if(
        state->Clips.begin(),
        state->Clips.end(),
        [&animationName](const RuntimeAnimationClip& clip)
        {
            return clip.Name == animationName;
        });

    if (it == state->Clips.end())
    {
        return SetError(errorMessage, "指定Animation名がありません: " + animationName);
    }
    if (it->Clip == nullptr)
    {
        return SetError(errorMessage, "指定AnimationClipがnullptrです");
    }

    state->AnimatorInstance.Play(it->Clip, restart);
    return true;
}

bool SkinnedAnimationRuntime::Play(
    std::size_t skinIndex,
    std::size_t sourceAnimationIndex,
    bool restart,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    const auto it = std::find_if(
        state->Clips.begin(),
        state->Clips.end(),
        [sourceAnimationIndex](const RuntimeAnimationClip& clip)
        {
            return clip.SourceAnimationIndex == sourceAnimationIndex;
        });

    if (it == state->Clips.end())
    {
        return SetError(errorMessage, "指定glTF Animation indexがありません");
    }
    if (it->Clip == nullptr)
    {
        return SetError(errorMessage, "指定AnimationClipがnullptrです");
    }

    state->AnimatorInstance.Play(it->Clip, restart);
    return true;
}

bool SkinnedAnimationRuntime::Pause(
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    state->AnimatorInstance.Pause();
    return true;
}

bool SkinnedAnimationRuntime::Resume(
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    state->AnimatorInstance.Resume();
    return true;
}

bool SkinnedAnimationRuntime::Stop(
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    state->AnimatorInstance.Stop();
    return true;
}

bool SkinnedAnimationRuntime::SetLoop(
    std::size_t skinIndex,
    bool loop,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    state->AnimatorInstance.SetLoop(loop);
    return true;
}

bool SkinnedAnimationRuntime::SetSpeed(
    std::size_t skinIndex,
    float speed,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(speed) == false)
    {
        return SetError(errorMessage, "Animation Speedは有限値である必要があります");
    }

    SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    state->AnimatorInstance.SetSpeed(speed);
    return true;
}

bool SkinnedAnimationRuntime::GetAnimationNames(
    std::size_t skinIndex,
    std::vector<std::string>& outNames,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const SkinAnimationState* state = FindSkinState(skinIndex);
    if (state == nullptr)
    {
        outNames.clear();
        return SetError(errorMessage, "指定SkinIndexのAnimation Runtime Stateがありません");
    }

    outNames.clear();
    outNames.reserve(state->Clips.size());
    for (const RuntimeAnimationClip& clip : state->Clips)
    {
        outNames.emplace_back(clip.Name);
    }

    return true;
}

bool SkinnedAnimationRuntime::ApplyPoseToSkin(
    const SkinAnimationState& state,
    std::string* errorMessage)
{
    if (m_TargetAsset == nullptr)
    {
        return SetError(errorMessage, "Animation RuntimeがSkinnedMeshRuntimeAssetへAttachされていません");
    }

    const Skeleton* animatorSkeleton = state.AnimatorInstance.GetSkeleton();
    if (animatorSkeleton == nullptr)
    {
        return SetError(errorMessage, "Animator Skeletonがnullptrです");
    }

    const SkeletonPose& pose = state.AnimatorInstance.GetCurrentSkeletonPose();
    if (pose.GetBoneCount() != animatorSkeleton->GetBoneCount())
    {
        return SetError(errorMessage, "Animator評価済みPoseのBone数がSkeletonと一致しません");
    }

    bool found = false;

    // 1つのSkinを共有するBody / Clothes等へ同じ評価済みPoseをコピーします。
    // PrimitiveごとにAnimatorを進めるとUpdate順やFrame落ちで位相差が生まれる可能性があるため、
    // Animator評価はSkinごとに1回だけ行い、その完成Poseを配布する構造にしています。
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

        deformer->GetPose() = pose;
        found = true;
    }

    if (found == false)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    return true;
}

bool SkinnedAnimationRuntime::Update(
    float deltaTime,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (m_TargetAsset == nullptr)
    {
        return SetError(errorMessage, "Animation RuntimeがSkinnedMeshRuntimeAssetへAttachされていません");
    }
    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    for (SkinAnimationState& state : m_SkinStates)
    {
        // Motion未選択のSkinはBind/手動Poseを維持します。
        // AttachしただけでAnimatorのBind Poseを上書きしないことで、既存の手動Bone Debugと共存できます。
        if (state.AnimatorInstance.GetCurrentState().IsValid() == false)
        {
            continue;
        }

        state.AnimatorInstance.Update(deltaTime);

        if (ApplyPoseToSkin(state, errorMessage) == false)
        {
            return false;
        }
    }

    // Pose同期後に既存MeshDeformation経路を1回だけ通します。
    // SkeletalMeshDeformer::Update() -> CPU Skinning -> Mesh::SyncGeometry()の責務は既存側へ残します。
    return m_TargetAsset->Update(deltaTime, errorMessage);
}

} // namespace Gltf
} // namespace Raven
