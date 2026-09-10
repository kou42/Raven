#pragma once

#include "Raven/Animation/CharacterLocomotionRuntime.h"
#include "Raven/Gltf/SkinnedBlendTreeRuntime.h"

#include <cstddef>
#include <string>

namespace Raven
{

class CharacterController;
struct CharacterControllerInput;
struct TransformComponent;

// ============================================================================
// CharacterControllerDemoLocomotionRuntime
// ============================================================================
// CharacterControllerDemoLayerの既存BlendTree呼び出し規約を維持しつつ、同じ更新入口から
// Motion Matchingへ切り替えるためのDemo専用Bridgeです。
//
// 既存Demo LayerはSkinnedBlendTreeRuntimeを直接操作しているため、このBridgeは
// SkinnedBlendTreeRuntimeを継承し、Attach / Configure / Updateの呼び出し互換性を保ちます。
// Motion Matchingを選択した場合だけCharacterLocomotionRuntimeへ更新を委譲し、
// 同一SkinへBlendTreeとMotion Matchingが同時にPoseを書き込まないようにします。
class CharacterControllerDemoLocomotionRuntime final : public Gltf::SkinnedBlendTreeRuntime
{
public:
    CharacterControllerDemoLocomotionRuntime() = default;

    CharacterControllerDemoLocomotionRuntime(
        CharacterController& controller,
        CharacterControllerInput& input,
        TransformComponent& characterTransform)
        : m_Controller(&controller),
          m_Input(&input),
          m_CharacterTransform(&characterTransform)
    {
    }

    CharacterControllerDemoLocomotionRuntime& operator=(Gltf::SkinnedBlendTreeRuntime&& runtime);

    void BindCharacterContext(
        CharacterController& controller,
        CharacterControllerInput& input,
        TransformComponent& characterTransform);

    bool AttachFromGlb(
        const std::string& filePath,
        Gltf::SkinnedMeshRuntimeAsset& targetAsset,
        std::string* errorMessage = nullptr);

    bool Configure(
        std::size_t skinIndex,
        const Gltf::LocomotionBlendTreeConfig& config,
        std::string* errorMessage = nullptr);

    bool ConfigureSprint(
        std::size_t skinIndex,
        const Gltf::LocomotionBlendTreeConfig& config,
        std::string* errorMessage = nullptr);

    bool SetMode(
        CharacterLocomotionRuntimeMode mode,
        std::size_t skinIndex,
        std::string* errorMessage = nullptr);

    CharacterLocomotionRuntimeMode GetMode() const
    {
        return m_Mode;
    }

    bool GetRuntimeDebugInfo(CharacterLocomotionRuntimeDebugInfo& outInfo) const
    {
        return m_RuntimeSelector.GetDebugInfo(outInfo);
    }

    bool GetInertializationBoneDebugInfo(
        BoneIndex boneIndex,
        PoseInertializerBoneDebugInfo& outInfo) const
    {
        return m_RuntimeSelector.GetInertializationBoneDebugInfo(boneIndex, outInfo);
    }

    bool Update(float deltaTime, std::string* errorMessage = nullptr);

private:
    bool ActivateBlendTree(std::size_t skinIndex, std::string* errorMessage);
    bool ActivateMotionMatching(std::size_t skinIndex, std::string* errorMessage);
    void ResetMotionMatchingState();

private:
    CharacterController* m_Controller = nullptr;
    CharacterControllerInput* m_Input = nullptr;
    TransformComponent* m_CharacterTransform = nullptr;

    Gltf::SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    std::string m_SourceFilePath;

    Gltf::SkinnedMotionMatchingRuntime m_MotionMatchingRuntime{};
    CharacterLocomotionRuntime m_RuntimeSelector{};
    CharacterLocomotionRuntimeMode m_Mode = CharacterLocomotionRuntimeMode::BlendTree;
    std::size_t m_ConfiguredSkinIndex = Gltf::InvalidGltfIndex;
};

} // namespace Raven
