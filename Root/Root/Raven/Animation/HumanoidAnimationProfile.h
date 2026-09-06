// Raven/Animation/HumanoidAnimationProfile.h
#pragma once

#include <string>

namespace Raven
{

// ============================================================================
// HumanoidLocomotionProfile
// ============================================================================
// Humanoid Assetごとに異なるIdle / Walk / Run / SprintのAnimation名と移動速度設定をまとめます。
//
// CharacterControllerDemoLayerのような利用側へAsset固有値を直接書くと、別Humanoidへ差し替えるたびに
// Gameplay / Debugコードを変更する必要があります。そのためAsset固有の「初期設定」をProfileへ分離し、
// Runtime側はこの設定を受け取ってBlendTreeを構築するだけにします。
//
// AuthoredMotionSpeedはClipを1.0倍再生したときに想定する水平移動速度です。
// ThresholdはBlendTree上でそのClipが100%になるSpeed Parameter値です。
// 現在は両者を同じ値にできますが、役割が異なるため別Fieldとして保持します。
// CharacterControllerのWalkSpeed / RunSpeed / SprintSpeedはGameplay上の目標速度であり、
// このProfileの値とは独立です。現在の数値が一致していても設定元は共有せず、どちらかの変更が
// 他方へ暗黙に伝播しない構造にします。
struct HumanoidLocomotionProfile
{
    std::string IdleAnimationName = "Idle";
    std::string WalkAnimationName = "Walk";
    std::string RunAnimationName = "Run";
    std::string SprintAnimationName = "Sprint";

    float IdleThreshold = 0.0f;
    float WalkThreshold = 1.8f;
    float RunThreshold = 5.5f;
    float SprintThreshold = 8.0f;

    float WalkAuthoredMotionSpeed = 1.8f;
    float RunAuthoredMotionSpeed = 5.5f;
    float SprintAuthoredMotionSpeed = 8.0f;
};

// ============================================================================
// HumanoidActionProfile
// ============================================================================
// Locomotion BlendTree外で一時再生するGameplay Action用Clip名をまとめます。
// Dashの移動ロジックはCharacterController側、Asset固有のRoll Clip名はProfile側へ分離し、
// Demo LayerへQuaternius固有名を直書きしない構造を維持します。
struct HumanoidActionProfile
{
    std::string DashAnimationName = "Roll";
};

// ============================================================================
// HumanoidAnimationProfile
// ============================================================================
// 1体のHumanoid Assetに対するAnimation設定の入口です。
// Locomotionと一時ActionのAsset固有設定を同じProfileへ集約し、Gameplay側は正式Clip名を知りません。
struct HumanoidAnimationProfile
{
    HumanoidLocomotionProfile Locomotion{};
    HumanoidActionProfile Actions{};
};

// Raven_human_test.glb専用の初期設定を返します。
// Asset固有値はこのFactoryへ集約し、CharacterControllerDemoLayerからGLB固有知識を分離します。
inline HumanoidAnimationProfile CreateRavenHumanTestAnimationProfile()
{
    HumanoidAnimationProfile profile{};
    profile.Locomotion.IdleAnimationName = "Idle";
    profile.Locomotion.WalkAnimationName = "Walk";
    profile.Locomotion.RunAnimationName = "Run";
    profile.Locomotion.SprintAnimationName = "Sprint";
    profile.Locomotion.IdleThreshold = 0.0f;
    profile.Locomotion.WalkThreshold = 1.8f;
    profile.Locomotion.RunThreshold = 5.5f;
    profile.Locomotion.SprintThreshold = 8.0f;
    profile.Locomotion.WalkAuthoredMotionSpeed = 1.8f;
    profile.Locomotion.RunAuthoredMotionSpeed = 5.5f;
    profile.Locomotion.SprintAuthoredMotionSpeed = 8.0f;
    profile.Actions.DashAnimationName = "Roll";
    return profile;
}

// Quaternius Universal Animation Library 1 Standard専用の初期設定を返します。
// JSON Profileが正規の設定元ですが、Asset欠落・破損時にも同じGLBの正式Clip名へfallbackできるよう、
// C++側にも最小限のAsset固有既定値を持たせます。Raven Human用Factoryと混在させないことで、
// fallback経路だけ別Skeleton向けのClip名を探してAnimation初期化に失敗することを防ぎます。
inline HumanoidAnimationProfile CreateQuaterniusUAL1StandardAnimationProfile()
{
    HumanoidAnimationProfile profile{};
    profile.Locomotion.IdleAnimationName = "Idle_Loop";
    profile.Locomotion.WalkAnimationName = "Walk_Loop";
    profile.Locomotion.RunAnimationName = "Jog_Fwd_Loop";
    profile.Locomotion.SprintAnimationName = "Sprint_Loop";
    profile.Locomotion.IdleThreshold = 0.0f;
    profile.Locomotion.WalkThreshold = 1.8f;
    profile.Locomotion.RunThreshold = 5.5f;
    profile.Locomotion.SprintThreshold = 8.0f;
    profile.Locomotion.WalkAuthoredMotionSpeed = 1.8f;
    profile.Locomotion.RunAuthoredMotionSpeed = 5.5f;
    profile.Locomotion.SprintAuthoredMotionSpeed = 8.0f;
    profile.Actions.DashAnimationName = "Roll";
    return profile;
}

} // namespace Raven
