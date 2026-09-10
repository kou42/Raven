#pragma once

#include "Raven/Animation/CharacterMotionMatchingRuntimeDriver.h"
#include "Raven/Gltf/SkinnedBlendTreeRuntime.h"

#include <cstddef>
#include <string>

namespace Raven
{

class CharacterController;
struct CharacterControllerInput;
struct TransformComponent;

// CharacterのLocomotion評価方式をGameplay側で選択するための明示的なModeです。
// BlendTreeとMotion Matchingを同時に同じSkinへ書き込ませないことが最重要の契約です。
enum class CharacterLocomotionRuntimeMode
{
    BlendTree,
    MotionMatching
};

struct CharacterLocomotionRuntimeDebugInfo
{
    CharacterLocomotionRuntimeMode Mode = CharacterLocomotionRuntimeMode::BlendTree;
    bool Active = false;
    CharacterMotionMatchingRuntimeDebugInfo MotionMatching{};
};

// ============================================================================
// CharacterLocomotionRuntime
// ============================================================================
// CharacterControllerDemoLayerなどGameplay側が、BlendTreeとMotion Matchingの排他的な
// 更新規則を個別に実装しないための小さな選択境界です。
//
// Runtime本体の所有権は既存クラスへ残します。このクラスは非所有参照だけを保持し、
// SceneInstance -> SkinnedMeshRuntimeAssetという既存Lifetime境界を変更しません。
class CharacterLocomotionRuntime
{
public:
    void Reset();

    bool UseBlendTree(
        Gltf::SkinnedBlendTreeRuntime& runtime,
        std::size_t skinIndex,
        std::string* errorMessage = nullptr);

    bool UseMotionMatching(
        Gltf::SkinnedMotionMatchingRuntime& runtime,
        std::size_t skinIndex,
        const Gltf::SkinnedMotionMatchingConfig& runtimeConfig,
        const CharacterTrajectoryPredictorConfig& predictorConfig = {},
        std::string* errorMessage = nullptr);

    bool Update(
        const CharacterController& controller,
        const CharacterControllerInput& input,
        const TransformComponent& characterTransform,
        float deltaTime,
        std::string* errorMessage = nullptr);

    CharacterLocomotionRuntimeMode GetMode() const
    {
        return m_Mode;
    }

    bool IsActive() const
    {
        return m_Active;
    }

    bool GetDebugInfo(CharacterLocomotionRuntimeDebugInfo& outInfo) const;

    bool GetInertializationBoneDebugInfo(
        BoneIndex boneIndex,
        PoseInertializerBoneDebugInfo& outInfo) const;

private:
    CharacterLocomotionRuntimeMode m_Mode = CharacterLocomotionRuntimeMode::BlendTree;
    Gltf::SkinnedBlendTreeRuntime* m_BlendTreeRuntime = nullptr;
    std::size_t m_BlendTreeSkinIndex = Gltf::InvalidGltfIndex;
    CharacterMotionMatchingRuntimeDriver m_MotionMatchingDriver{};
    bool m_Active = false;
};

} // namespace Raven
