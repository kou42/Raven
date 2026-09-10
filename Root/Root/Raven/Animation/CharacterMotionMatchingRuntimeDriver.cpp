#include "Raven/Animation/CharacterMotionMatchingRuntimeDriver.h"

#include <cmath>
#include <limits>
#include <utility>

namespace Raven
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

} // namespace

bool CharacterMotionMatchingRuntimeDriver::BuildDefaultConfig(
    const Skeleton& skeleton,
    Gltf::SkinnedMotionMatchingConfig& outConfig,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    outConfig = Gltf::SkinnedMotionMatchingConfig{};

    if (skeleton.GetBoneCount() == 0u)
    {
        return SetError(errorMessage, "Motion Matching既定設定を構築するSkeletonにBoneがありません");
    }

    BoneIndex rootBone = InvalidBoneIndex;
    for (std::size_t boneIndex = 0u; boneIndex < skeleton.GetBoneCount(); ++boneIndex)
    {
        const BoneIndex index = static_cast<BoneIndex>(boneIndex);
        if (skeleton.GetBone(index).Parent == InvalidBoneIndex)
        {
            rootBone = index;
            break;
        }
    }

    if (rootBone == InvalidBoneIndex)
    {
        return SetError(errorMessage, "Motion Matching既定設定のRoot Boneを解決できません");
    }

    outConfig.SampleRate = 30.0f;
    outConfig.NormalizeFeatures = true;
    outConfig.PoseFeatures.RootBone = rootBone;
    outConfig.TrajectoryFeatures.RootBone = rootBone;

    // 初期統合ではAsset固有Bone名をDemoコードへ直書きしません。
    // Root以外をすべてPose Featureへ採用することで任意Humanoidでも必ず同じ規約で開始でき、
    // 後からProfile側へFeature Bone選択を移した際にもRuntime APIを変更せず最適化できます。
    outConfig.PoseFeatures.PoseBones.reserve(skeleton.GetBoneCount() - 1u);
    for (std::size_t boneIndex = 0u; boneIndex < skeleton.GetBoneCount(); ++boneIndex)
    {
        const BoneIndex index = static_cast<BoneIndex>(boneIndex);
        if (index == rootBone)
        {
            continue;
        }
        outConfig.PoseFeatures.PoseBones.push_back(index);
    }

    // CharacterTrajectoryPredictorはこの時間Offsetを小刻みに積分して将来位置を作ります。
    // 近・中・遠の3点を持たせ、切替直後だけでなく進行方向の継続性も検索Costへ反映します。
    outConfig.TrajectoryFeatures.FutureTimeOffsets = { 0.20f, 0.40f, 0.60f };

    return true;
}

bool CharacterMotionMatchingRuntimeDriver::Configure(
    Gltf::SkinnedMotionMatchingRuntime& runtime,
    std::size_t skinIndex,
    const Gltf::SkinnedMotionMatchingConfig& runtimeConfig,
    const CharacterTrajectoryPredictorConfig& predictorConfig,
    std::string* errorMessage)
{
    Reset();

    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (skinIndex == Gltf::InvalidGltfIndex)
    {
        return SetError(errorMessage, "Motion Matching Driverへ無効なSkinIndexが指定されました");
    }
    if (std::isfinite(predictorConfig.SimulationStep) == false
        || predictorConfig.SimulationStep <= 0.0f)
    {
        return SetError(errorMessage, "Trajectory Predictor SimulationStepは0より大きい有限値である必要があります");
    }

    const Skeleton* skeleton = runtime.GetSkeleton(skinIndex);
    if (skeleton == nullptr)
    {
        return SetError(errorMessage, "Motion Matching RuntimeからSkeletonを取得できません");
    }

    if (runtime.Configure(skinIndex, runtimeConfig, errorMessage) == false)
    {
        return false;
    }

    m_Runtime = &runtime;
    m_SkinIndex = skinIndex;
    m_PoseFeatures = runtimeConfig.PoseFeatures;
    m_TrajectoryFeatures = runtimeConfig.TrajectoryFeatures;
    m_PredictorConfig = predictorConfig;
    m_Configured = true;
    return true;
}

void CharacterMotionMatchingRuntimeDriver::Reset()
{
    m_Runtime = nullptr;
    m_SkinIndex = Gltf::InvalidGltfIndex;
    m_PoseFeatures = MotionPoseFeatureConfig{};
    m_TrajectoryFeatures = MotionTrajectoryFeatureConfig{};
    m_PredictorConfig = CharacterTrajectoryPredictorConfig{};
    m_Configured = false;
}

bool CharacterMotionMatchingRuntimeDriver::Update(
    const CharacterController& controller,
    const CharacterControllerInput& input,
    const TransformComponent& characterTransform,
    float deltaTime,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (IsConfigured() == false)
    {
        return SetError(errorMessage, "Character Motion Matching Runtime DriverがConfigureされていません");
    }
    if (std::isfinite(deltaTime) == false || deltaTime <= 0.0f)
    {
        return SetError(errorMessage, "Character Motion Matching deltaTimeは0より大きい有限値である必要があります");
    }

    const Skeleton* skeleton = m_Runtime->GetSkeleton(m_SkinIndex);
    const SkeletonPose* currentPose = m_Runtime->GetCurrentPose(m_SkinIndex);
    const SkeletonPose* previousPose = m_Runtime->GetPreviousPose(m_SkinIndex);
    if (skeleton == nullptr || currentPose == nullptr || previousPose == nullptr)
    {
        return SetError(errorMessage, "Character Motion Matching Query生成に必要なPose履歴を取得できません");
    }

    MotionSearchQuery query{};
    if (CharacterMotionMatchingAdapter::BuildPredictedQuery(
            controller,
            input,
            characterTransform,
            *skeleton,
            *currentPose,
            *previousPose,
            deltaTime,
            m_PoseFeatures,
            m_TrajectoryFeatures,
            m_PredictorConfig,
            query) == false)
    {
        return SetError(errorMessage, "Character状態からMotion Matching Queryを構築できません");
    }

    return m_Runtime->Update(m_SkinIndex, query, deltaTime, errorMessage);
}

bool CharacterMotionMatchingRuntimeDriver::GetDebugInfo(
    CharacterMotionMatchingRuntimeDebugInfo& outInfo) const
{
    outInfo = CharacterMotionMatchingRuntimeDebugInfo{};

    if (IsConfigured() == false)
    {
        return false;
    }

    const MotionMatcher* matcher = m_Runtime->GetMotionMatcher(m_SkinIndex);
    if (matcher == nullptr)
    {
        return false;
    }

    outInfo.Active = true;
    outInfo.HasSelection = matcher->HasSelection();
    outInfo.SelectedFrameIndex = matcher->GetSelectedFrameIndex();
    outInfo.ClipIndex = matcher->GetCurrentClipIndex();
    outInfo.ClipTime = matcher->GetCurrentTime();
    outInfo.SearchCost = matcher->GetLastSearchCost();
    outInfo.Inertializing = matcher->IsInertializing();
    outInfo.InertializationElapsedTime = matcher->GetInertializationElapsedTime();
    return true;
}

bool CharacterMotionMatchingRuntimeDriver::GetInertializationBoneDebugInfo(
    BoneIndex boneIndex,
    PoseInertializerBoneDebugInfo& outInfo) const
{
    if (IsConfigured() == false)
    {
        return false;
    }

    return m_Runtime->GetInertializationBoneDebugInfo(
        m_SkinIndex,
        boneIndex,
        outInfo);
}

} // namespace Raven
