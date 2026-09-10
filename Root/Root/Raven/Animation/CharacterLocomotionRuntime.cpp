#include "Raven/Animation/CharacterLocomotionRuntime.h"

#include "Raven/Character/CharacterController.h"
#include "Raven/Scene/Components.h"

#include <cmath>

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

void CharacterLocomotionRuntime::Reset()
{
    m_Mode = CharacterLocomotionRuntimeMode::BlendTree;
    m_BlendTreeRuntime = nullptr;
    m_BlendTreeSkinIndex = Gltf::InvalidGltfIndex;
    m_MotionMatchingDriver.Reset();
    m_Active = false;
}

bool CharacterLocomotionRuntime::UseBlendTree(
    Gltf::SkinnedBlendTreeRuntime& runtime,
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    if (skinIndex == Gltf::InvalidGltfIndex)
    {
        return SetError(errorMessage, "BlendTree LocomotionのSkinIndexが無効です");
    }

    // Mode切替時に前MotionMatcherのPose履歴やInertialization状態を再利用しないよう、
    // Driverを明示的にResetして単一Writer契約を確立します。
    m_MotionMatchingDriver.Reset();
    m_Mode = CharacterLocomotionRuntimeMode::BlendTree;
    m_BlendTreeRuntime = &runtime;
    m_BlendTreeSkinIndex = skinIndex;
    m_Active = true;
    return true;
}

bool CharacterLocomotionRuntime::UseMotionMatching(
    Gltf::SkinnedMotionMatchingRuntime& runtime,
    std::size_t skinIndex,
    const Gltf::SkinnedMotionMatchingConfig& runtimeConfig,
    const CharacterTrajectoryPredictorConfig& predictorConfig,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    CharacterMotionMatchingRuntimeDriver driver{};
    if (driver.Configure(
            runtime,
            skinIndex,
            runtimeConfig,
            predictorConfig,
            errorMessage) == false)
    {
        return false;
    }

    // Configure成功後だけModeを切り替えます。失敗時に既存BlendTreeを失わないための
    // transactionalな切替で、Debug DemoでもAnimationが突然停止することを避けます。
    m_BlendTreeRuntime = nullptr;
    m_BlendTreeSkinIndex = Gltf::InvalidGltfIndex;
    m_MotionMatchingDriver = std::move(driver);
    m_Mode = CharacterLocomotionRuntimeMode::MotionMatching;
    m_Active = true;
    return true;
}

bool CharacterLocomotionRuntime::Update(
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
    if (m_Active == false)
    {
        return true;
    }
    if (std::isfinite(deltaTime) == false || deltaTime <= 0.0f)
    {
        return SetError(errorMessage, "Locomotion Runtime deltaTimeは0より大きい有限値である必要があります");
    }

    if (m_Mode == CharacterLocomotionRuntimeMode::MotionMatching)
    {
        return m_MotionMatchingDriver.Update(
            controller,
            input,
            characterTransform,
            deltaTime,
            errorMessage);
    }

    if (m_BlendTreeRuntime == nullptr
        || m_BlendTreeSkinIndex == Gltf::InvalidGltfIndex)
    {
        return SetError(errorMessage, "BlendTree Locomotion Runtimeが接続されていません");
    }

    // BlendTree経路では既存CharacterController APIを唯一のSpeed Parameter同期入口として再利用します。
    // 入力Flagではなく衝突解決後の実水平速度を使う従来契約をMode抽象化後も維持します。
    if (controller.UpdateLocomotionAnimation(
            *m_BlendTreeRuntime,
            m_BlendTreeSkinIndex,
            errorMessage) == false)
    {
        return false;
    }

    return m_BlendTreeRuntime->Update(deltaTime, errorMessage);
}

bool CharacterLocomotionRuntime::GetDebugInfo(CharacterLocomotionRuntimeDebugInfo& outInfo) const
{
    outInfo = CharacterLocomotionRuntimeDebugInfo{};
    outInfo.Mode = m_Mode;
    outInfo.Active = m_Active;

    if (m_Active == false)
    {
        return true;
    }

    if (m_Mode == CharacterLocomotionRuntimeMode::MotionMatching)
    {
        return m_MotionMatchingDriver.GetDebugInfo(outInfo.MotionMatching);
    }

    return true;
}

bool CharacterLocomotionRuntime::GetInertializationBoneDebugInfo(
    BoneIndex boneIndex,
    PoseInertializerBoneDebugInfo& outInfo) const
{
    if (m_Active == false
        || m_Mode != CharacterLocomotionRuntimeMode::MotionMatching)
    {
        return false;
    }

    return m_MotionMatchingDriver.GetInertializationBoneDebugInfo(boneIndex, outInfo);
}

} // namespace Raven
