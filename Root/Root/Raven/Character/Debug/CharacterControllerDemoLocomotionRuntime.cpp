#include "Raven/Character/Debug/CharacterControllerDemoLocomotionRuntime.h"

#include "Raven/Animation/CharacterMotionMatchingRuntimeDriver.h"
#include "Raven/Character/CharacterController.h"
#include "Raven/Gltf/SkinnedMeshRuntime.h"
#include "Raven/Scene/Components.h"

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

CharacterControllerDemoLocomotionRuntime& CharacterControllerDemoLocomotionRuntime::operator=(
    Gltf::SkinnedBlendTreeRuntime&& runtime)
{
    static_cast<Gltf::SkinnedBlendTreeRuntime&>(*this) = std::move(runtime);

    // DemoLayerの既存Reset代入と互換にしつつ、Motion Matching側の非所有参照も同時に破棄します。
    ResetMotionMatchingState();
    m_TargetAsset = nullptr;
    m_SourceFilePath.clear();
    m_ConfiguredSkinIndex = Gltf::InvalidGltfIndex;
    m_Mode = CharacterLocomotionRuntimeMode::BlendTree;
    return *this;
}

void CharacterControllerDemoLocomotionRuntime::BindCharacterContext(
    CharacterController& controller,
    CharacterControllerInput& input,
    TransformComponent& characterTransform)
{
    m_Controller = &controller;
    m_Input = &input;
    m_CharacterTransform = &characterTransform;
}

bool CharacterControllerDemoLocomotionRuntime::AttachFromGlb(
    const std::string& filePath,
    Gltf::SkinnedMeshRuntimeAsset& targetAsset,
    std::string* errorMessage)
{
    if (Gltf::SkinnedBlendTreeRuntime::AttachFromGlb(
            filePath,
            targetAsset,
            errorMessage) == false)
    {
        return false;
    }

    // Motion MatchingはMode選択時に現在Deformer Poseから履歴を取り直すため、ここではまだAttachしません。
    // BlendTreeでしばらく再生した後に切り替えても、初期Pose履歴がScene生成時へ巻き戻らないようにします。
    m_TargetAsset = &targetAsset;
    m_SourceFilePath = filePath;
    return true;
}

bool CharacterControllerDemoLocomotionRuntime::Configure(
    std::size_t skinIndex,
    const Gltf::LocomotionBlendTreeConfig& config,
    std::string* errorMessage)
{
    if (Gltf::SkinnedBlendTreeRuntime::Configure(
            skinIndex,
            config,
            errorMessage) == false)
    {
        return false;
    }

    m_ConfiguredSkinIndex = skinIndex;
    return ActivateBlendTree(skinIndex, errorMessage);
}

bool CharacterControllerDemoLocomotionRuntime::ConfigureSprint(
    std::size_t skinIndex,
    const Gltf::LocomotionBlendTreeConfig& config,
    std::string* errorMessage)
{
    if (Gltf::SkinnedBlendTreeRuntime::ConfigureSprint(
            skinIndex,
            config,
            errorMessage) == false)
    {
        return false;
    }

    m_ConfiguredSkinIndex = skinIndex;
    return ActivateBlendTree(skinIndex, errorMessage);
}

bool CharacterControllerDemoLocomotionRuntime::SetMode(
    CharacterLocomotionRuntimeMode mode,
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (skinIndex == Gltf::InvalidGltfIndex)
    {
        return SetError(errorMessage, "Humanoid Locomotion RuntimeのSkinIndexが無効です");
    }

    if (mode == CharacterLocomotionRuntimeMode::MotionMatching)
    {
        return ActivateMotionMatching(skinIndex, errorMessage);
    }

    return ActivateBlendTree(skinIndex, errorMessage);
}

bool CharacterControllerDemoLocomotionRuntime::Update(
    float deltaTime,
    std::string* errorMessage)
{
    if (m_RuntimeSelector.IsActive() == false)
    {
        // Configure前は従来Runtimeの挙動へfallbackします。通常Demo経路ではConfigure後に必ずActiveです。
        return Gltf::SkinnedBlendTreeRuntime::Update(deltaTime, errorMessage);
    }

    if (m_Controller == nullptr
        || m_Input == nullptr
        || m_CharacterTransform == nullptr)
    {
        return SetError(errorMessage, "Character Locomotion RuntimeのCharacter Contextが未接続です");
    }

    return m_RuntimeSelector.Update(
        *m_Controller,
        *m_Input,
        *m_CharacterTransform,
        deltaTime,
        errorMessage);
}

bool CharacterControllerDemoLocomotionRuntime::ActivateBlendTree(
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (m_ConfiguredSkinIndex != Gltf::InvalidGltfIndex
        && skinIndex != m_ConfiguredSkinIndex)
    {
        return SetError(errorMessage, "Configure済みBlendTreeと指定SkinIndexが一致しません");
    }

    if (m_RuntimeSelector.UseBlendTree(*this, skinIndex, errorMessage) == false)
    {
        return false;
    }

    m_Mode = CharacterLocomotionRuntimeMode::BlendTree;
    return true;
}

bool CharacterControllerDemoLocomotionRuntime::ActivateMotionMatching(
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (m_TargetAsset == nullptr)
    {
        return SetError(errorMessage, "Motion Matching用SkinnedMeshRuntimeAssetが未接続です");
    }
    if (m_SourceFilePath.empty() == true)
    {
        return SetError(errorMessage, "Motion Matching用GLB Pathが空です");
    }

    // Mode切替ごとにRuntimeを再Attachし、現在のDeformer PoseをCurrent/Previous履歴へ採用します。
    // Database再構築コストはDemo用途では許容し、BlendTree再生後の古い初期Poseを速度履歴へ混ぜないことを優先します。
    Gltf::SkinnedMotionMatchingRuntime motionMatchingRuntime{};
    if (motionMatchingRuntime.AttachFromGlb(
            m_SourceFilePath,
            *m_TargetAsset,
            errorMessage) == false)
    {
        return false;
    }

    const Skeleton* skeleton = motionMatchingRuntime.GetSkeleton(skinIndex);
    if (skeleton == nullptr)
    {
        return SetError(errorMessage, "Motion Matching RuntimeからSkeletonを取得できません");
    }

    Gltf::SkinnedMotionMatchingConfig config{};
    if (CharacterMotionMatchingRuntimeDriver::BuildDefaultConfig(
            *skeleton,
            config,
            errorMessage) == false)
    {
        return false;
    }

    // 一時RuntimeでAttach/Configを完了してからMemberへ移すことで、失敗時は現在のBlendTreeを維持します。
    m_MotionMatchingRuntime = std::move(motionMatchingRuntime);
    if (m_RuntimeSelector.UseMotionMatching(
            m_MotionMatchingRuntime,
            skinIndex,
            config,
            CharacterTrajectoryPredictorConfig{},
            errorMessage) == false)
    {
        ResetMotionMatchingState();
        return false;
    }

    m_Mode = CharacterLocomotionRuntimeMode::MotionMatching;
    return true;
}

void CharacterControllerDemoLocomotionRuntime::ResetMotionMatchingState()
{
    m_RuntimeSelector.Reset();
    m_MotionMatchingRuntime = Gltf::SkinnedMotionMatchingRuntime{};
}

} // namespace Raven
