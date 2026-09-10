#include "Raven/Gltf/SkinnedMotionMatchingRuntime.h"

#include "Raven/Animation/SkeletalMeshDeformer.h"
#include "Raven/Gltf/AnimationImporter.h"
#include "Raven/Gltf/SkinImporter.h"
#include "Raven/Gltf/SkinnedMeshRuntime.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
        SetError(errorMessage, "Motion Matching対象のMeshDeformationInstanceがnullptrです");
        return nullptr;
    }

    MeshDeformer* baseDeformer = primitive.DeformationInstance->GetDeformer();
    if (baseDeformer == nullptr)
    {
        SetError(errorMessage, "Motion Matching対象のMeshDeformerがnullptrです");
        return nullptr;
    }

    SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
    if (skeletalDeformer == nullptr)
    {
        SetError(errorMessage, "Motion Matching対象のDeformerがSkeletalMeshDeformerではありません");
        return nullptr;
    }
    return skeletalDeformer;
}

bool SkeletonsMatch(const Skeleton& lhs, const Skeleton& rhs, std::string* errorMessage)
{
    if (lhs.GetBoneCount() != rhs.GetBoneCount())
    {
        return SetError(errorMessage, "Motion Matching SkeletonのBone数が一致しません");
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
                "Motion Matching SkeletonのBone順序/階層が一致しません。bone="
                    + std::to_string(boneIndex));
        }
    }
    return true;
}

} // namespace

SkinnedMotionMatchingRuntime::SkinState* SkinnedMotionMatchingRuntime::FindSkinState(std::size_t skinIndex)
{
    const auto it = std::find_if(
        m_SkinStates.begin(), m_SkinStates.end(),
        [skinIndex](const SkinState& state) { return state.SkinIndex == skinIndex; });
    return it == m_SkinStates.end() ? nullptr : &(*it);
}

const SkinnedMotionMatchingRuntime::SkinState* SkinnedMotionMatchingRuntime::FindSkinState(std::size_t skinIndex) const
{
    const auto it = std::find_if(
        m_SkinStates.begin(), m_SkinStates.end(),
        [skinIndex](const SkinState& state) { return state.SkinIndex == skinIndex; });
    return it == m_SkinStates.end() ? nullptr : &(*it);
}

bool SkinnedMotionMatchingRuntime::AttachFromGlb(
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
        return SetError(errorMessage, "Motion Matchingを接続するRuntime Skinned Primitiveがありません");
    }

    std::vector<ImportedSkin> importedSkins;
    if (SkinImporter::LoadFromGlb(filePath, importedSkins, errorMessage) == false)
    {
        return false;
    }

    std::vector<SkinState> states;
    for (const RuntimeSkinnedPrimitive& primitive : primitives)
    {
        const auto existing = std::find_if(
            states.begin(), states.end(),
            [&primitive](const SkinState& state) { return state.SkinIndex == primitive.SkinIndex; });
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
        if (AnimationImporter::LoadFromGlb(filePath, importedSkin, importedClips, errorMessage) == false)
        {
            return false;
        }

        SkinState state{};
        state.SkinIndex = primitive.SkinIndex;
        state.SkeletonData = &deformer->GetSkeleton();
        state.CurrentPose = deformer->GetPose();
        state.PreviousPose = state.CurrentPose;
        state.HasPoseHistory = true;
        state.Clips.reserve(importedClips.size());

        for (ImportedAnimationClip& importedClip : importedClips)
        {
            RuntimeClip runtimeClip{};
            runtimeClip.SourceAnimationIndex = importedClip.AnimationIndex;
            runtimeClip.Name = std::move(importedClip.Name);
            runtimeClip.Clip = std::make_shared<AnimationClip>(std::move(importedClip.Clip));
            if (runtimeClip.Clip == nullptr)
            {
                return SetError(errorMessage, "Motion Matching用AnimationClipの生成に失敗しました");
            }
            state.Clips.emplace_back(std::move(runtimeClip));
        }

        states.emplace_back(std::move(state));
    }

    if (states.empty())
    {
        return SetError(errorMessage, "Motion Matchingを接続できるSkinがありません");
    }

    m_TargetAsset = &targetAsset;
    m_SkinStates = std::move(states);
    return true;
}

bool SkinnedMotionMatchingRuntime::Configure(
    std::size_t skinIndex,
    const SkinnedMotionMatchingConfig& config,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    if (std::isfinite(config.SampleRate) == false || config.SampleRate <= 0.0f)
    {
        return SetError(errorMessage, "Motion Database SampleRateは0より大きい有限値である必要があります");
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->SkeletonData == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのMotion Matching Runtime Stateがありません");
    }
    if (state->Clips.empty())
    {
        return SetError(errorMessage, "Motion Databaseへ登録するAnimation Clipがありません");
    }

    std::shared_ptr<MotionDatabase> database = std::make_shared<MotionDatabase>();
    if (database == nullptr)
    {
        return SetError(errorMessage, "MotionDatabaseの生成に失敗しました");
    }

    for (const RuntimeClip& clip : state->Clips)
    {
        if (database->AddClip(clip.Clip) == false)
        {
            return SetError(errorMessage, "MotionDatabaseへのAnimation Clip登録に失敗しました");
        }
    }
    if (database->Build(config.SampleRate) == false
        || database->BuildPoseFeatures(*state->SkeletonData, config.PoseFeatures) == false
        || database->BuildTrajectoryFeatures(*state->SkeletonData, config.TrajectoryFeatures) == false)
    {
        return SetError(errorMessage, "MotionDatabaseのFeature構築に失敗しました");
    }

    MotionMatcher matcher;
    matcher.SetDatabase(database);
    matcher.SetConfig(config.Matcher);

    if (config.NormalizeFeatures == true)
    {
        if (database->BuildFeatureNormalization() == false)
        {
            return SetError(errorMessage, "MotionDatabase Feature正規化統計の構築に失敗しました");
        }
        if (matcher.SetNormalizedSearchWeights(config.Matcher.SearchWeights) == false)
        {
            return SetError(errorMessage, "MotionMatcher Search Weightの正規化に失敗しました");
        }
    }

    state->Database = std::move(database);
    state->Matcher = std::move(matcher);
    state->Matcher.Reset();
    state->Configured = true;
    return true;
}

bool SkinnedMotionMatchingRuntime::ApplyPoseToSkin(
    const SkinState& state,
    const SkeletonPose& pose,
    std::string* errorMessage)
{
    if (m_TargetAsset == nullptr || state.SkeletonData == nullptr)
    {
        return SetError(errorMessage, "Motion Matching RuntimeがSkinnedMeshRuntimeAssetへAttachされていません");
    }
    if (pose.GetBoneCount() != state.SkeletonData->GetBoneCount())
    {
        return SetError(errorMessage, "Motion Matching評価済みPoseのBone数がSkeletonと一致しません");
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
        if (SkeletonsMatch(*state.SkeletonData, deformer->GetSkeleton(), errorMessage) == false)
        {
            return false;
        }

        // Body / Clothesなど同一Skinを共有するPrimitiveへ、検索済みPoseを同一Frameで配布します。
        deformer->GetPose() = pose;
        found = true;
    }
    if (found == false)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }
    return true;
}

bool SkinnedMotionMatchingRuntime::Update(
    std::size_t skinIndex,
    const MotionSearchQuery& query,
    float deltaTime,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    if (m_TargetAsset == nullptr)
    {
        return SetError(errorMessage, "Motion Matching RuntimeがSkinnedMeshRuntimeAssetへAttachされていません");
    }
    if (std::isfinite(deltaTime) == false || deltaTime <= 0.0f)
    {
        return SetError(errorMessage, "Motion Matching deltaTimeは0より大きい有限値である必要があります");
    }

    SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->Configured == false || state->SkeletonData == nullptr)
    {
        return SetError(errorMessage, "指定SkinIndexのMotion Matching RuntimeがConfigureされていません");
    }

    SkeletonPose outputPose;
    if (state->Matcher.Update(*state->SkeletonData, query, deltaTime, outputPose) == false)
    {
        return SetError(errorMessage, "MotionMatcherの更新に失敗しました");
    }

    if (state->HasPoseHistory == true)
    {
        state->PreviousPose = state->CurrentPose;
    }
    else
    {
        state->PreviousPose = outputPose;
    }
    state->CurrentPose = outputPose;
    state->HasPoseHistory = true;

    if (ApplyPoseToSkin(*state, outputPose, errorMessage) == false)
    {
        return false;
    }

    // Pose配布後は既存のCPU Skinning -> Mesh::SyncGeometry()経路を再利用します。
    return m_TargetAsset->Update(deltaTime, errorMessage);
}

const Skeleton* SkinnedMotionMatchingRuntime::GetSkeleton(std::size_t skinIndex) const
{
    const SkinState* state = FindSkinState(skinIndex);
    return state != nullptr ? state->SkeletonData : nullptr;
}

const SkeletonPose* SkinnedMotionMatchingRuntime::GetCurrentPose(std::size_t skinIndex) const
{
    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->HasPoseHistory == false)
    {
        return nullptr;
    }
    return &state->CurrentPose;
}

const SkeletonPose* SkinnedMotionMatchingRuntime::GetPreviousPose(std::size_t skinIndex) const
{
    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->HasPoseHistory == false)
    {
        return nullptr;
    }
    return &state->PreviousPose;
}

bool SkinnedMotionMatchingRuntime::GetInertializationBoneDebugInfo(
    std::size_t skinIndex,
    BoneIndex boneIndex,
    PoseInertializerBoneDebugInfo& outInfo) const
{
    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->Configured == false)
    {
        return false;
    }
    return state->Matcher.GetInertializationBoneDebugInfo(boneIndex, outInfo);
}

const MotionMatcher* SkinnedMotionMatchingRuntime::GetMotionMatcher(std::size_t skinIndex) const
{
    const SkinState* state = FindSkinState(skinIndex);
    if (state == nullptr || state->Configured == false)
    {
        return nullptr;
    }
    return &state->Matcher;
}

} // namespace Gltf
} // namespace Raven
