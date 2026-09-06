// Raven/Character/Tests/CharacterSprintLocomotionSelfTests.h
#pragma once

#include <cassert>
#include <cmath>
#include <memory>
#include <string>

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Animation/BlendTree1D.h"
#include "Raven/Animation/HumanoidAnimationProfileSerialization.h"

namespace Raven::tests
{
namespace sprint_locomotion_tests
{

inline bool NearlyEqual(float a, float b, float tolerance = 1.0e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

inline std::shared_ptr<AnimationClip> MakeClip(float duration)
{
    return std::make_shared<AnimationClip>(duration);
}

inline void RunFourChildBlendTreeTest()
{
    BlendTree1D tree;
    assert(tree.AddChild(0.0f, MakeClip(1.0f)));
    assert(tree.AddChild(2.0f, MakeClip(0.9f)));
    assert(tree.AddChild(5.5f, MakeClip(0.7f)));
    assert(tree.AddChild(8.0f, MakeClip(0.55f)));
    assert(tree.GetChildCount() == 4u);

    BlendTree1DDebugInfo info{};

    // Run=5.5とSprint=8.0の中点では、Jog/Sprintが50%ずつ選択されることを固定します。
    assert(tree.GetDebugInfo(6.75f, info));
    assert(info.LeftChildIndex == 2u);
    assert(info.RightChildIndex == 3u);
    assert(NearlyEqual(info.LeftWeight, 0.5f));
    assert(NearlyEqual(info.RightWeight, 0.5f));
    assert(info.IsClamped == false);

    // Sprint Threshold到達時は4番目のChildだけが100%になります。
    assert(tree.GetDebugInfo(8.0f, info));
    assert(info.LeftChildIndex == 3u);
    assert(info.RightChildIndex == 3u);
    assert(NearlyEqual(info.LeftWeight, 1.0f));
    assert(NearlyEqual(info.RightWeight, 0.0f));

    // Runtime TuningでThresholdを変更してもChild順を変えず、Run/Sprint区間だけを再配置します。
    assert(tree.SetThresholds({ 0.0f, 2.0f, 6.0f, 10.0f }));
    assert(tree.GetDebugInfo(8.0f, info));
    assert(info.LeftChildIndex == 2u);
    assert(info.RightChildIndex == 3u);
    assert(NearlyEqual(info.LeftWeight, 0.5f));
    assert(NearlyEqual(info.RightWeight, 0.5f));
}

inline void RunSprintProfileRoundTripTest()
{
    HumanoidAnimationProfile source = CreateRavenHumanTestAnimationProfile();
    source.Locomotion.IdleAnimationName = "Idle_Loop";
    source.Locomotion.WalkAnimationName = "Walk_Loop";
    source.Locomotion.RunAnimationName = "Jog_Fwd_Loop";
    source.Locomotion.SprintAnimationName = "Sprint_Loop";
    source.Locomotion.IdleThreshold = 0.0f;
    source.Locomotion.WalkThreshold = 1.8f;
    source.Locomotion.RunThreshold = 5.5f;
    source.Locomotion.SprintThreshold = 8.0f;
    source.Locomotion.WalkAuthoredMotionSpeed = 1.8f;
    source.Locomotion.RunAuthoredMotionSpeed = 5.5f;
    source.Locomotion.SprintAuthoredMotionSpeed = 8.0f;

    std::string serialized;
    std::string errorMessage;
    assert(SerializeHumanoidAnimationProfile(source, serialized, &errorMessage));

    HumanoidAnimationProfile restored{};
    assert(DeserializeHumanoidAnimationProfile(serialized, restored, &errorMessage));
    assert(restored.Locomotion.SprintAnimationName == "Sprint_Loop");
    assert(NearlyEqual(restored.Locomotion.SprintThreshold, 8.0f));
    assert(NearlyEqual(restored.Locomotion.SprintAuthoredMotionSpeed, 8.0f));
}

inline void RunLegacyProfileSprintFallbackTest()
{
    // Sprint追加前のversion 1 Profileを模したJSONです。
    // Run値が新しいSprint既定値8.0を超えていても、Deserialize側がRunより上へSprint値を補完する必要があります。
    constexpr const char* LegacyProfile = R"json(
{
  "type": "RavenHumanoidAnimationProfile",
  "version": 1,
  "locomotion": {
    "idleAnimation": "Idle",
    "walkAnimation": "Walk",
    "runAnimation": "Run",
    "idleThreshold": 0.0,
    "walkThreshold": 2.0,
    "runThreshold": 10.0,
    "walkAuthoredMotionSpeed": 2.0,
    "runAuthoredMotionSpeed": 9.5
  }
}
)json";

    HumanoidAnimationProfile restored{};
    std::string errorMessage;
    assert(DeserializeHumanoidAnimationProfile(LegacyProfile, restored, &errorMessage));
    assert(restored.Locomotion.SprintAnimationName.empty() == false);
    assert(restored.Locomotion.SprintThreshold > restored.Locomotion.RunThreshold);
    assert(restored.Locomotion.SprintAuthoredMotionSpeed > restored.Locomotion.RunAuthoredMotionSpeed);
}

} // namespace sprint_locomotion_tests

// Sprint追加で増えた4 Child BlendTreeとProfile互換だけを集中的に検証します。
// Renderer / GLB / OpenGL Contextへ依存しないため、Debug起動直後に安全に実行できます。
inline void RunCharacterSprintLocomotionSelfTests()
{
    sprint_locomotion_tests::RunFourChildBlendTreeTest();
    sprint_locomotion_tests::RunSprintProfileRoundTripTest();
    sprint_locomotion_tests::RunLegacyProfileSprintFallbackTest();
}

} // namespace Raven::tests
