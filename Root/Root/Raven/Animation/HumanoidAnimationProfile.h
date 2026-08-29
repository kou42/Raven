// Raven/Animation/HumanoidAnimationProfile.h
#pragma once

#include <string>

namespace Raven
{

// ============================================================================
// HumanoidLocomotionProfile
// ============================================================================
// Humanoid Assetごとに異なるIdle / Walk / RunのAnimation名と移動速度設定をまとめます。
//
// CharacterControllerDemoLayerのような利用側へAsset固有値を直接書くと、別Humanoidへ差し替えるたびに
// Gameplay / Debugコードを変更する必要があります。そのためAsset固有の「初期設定」をProfileへ分離し、
// Runtime側はこの設定を受け取ってBlendTreeを構築するだけにします。
//
// AuthoredMotionSpeedはClipを1.0倍再生したときに想定する水平移動速度です。
// ThresholdはBlendTree上でそのClipが100%になるSpeed Parameter値です。
// 現在は両者を同じ値にできますが、役割が異なるため別Fieldとして保持します。
struct HumanoidLocomotionProfile
{
    std::string IdleAnimationName = "Idle";
    std::string WalkAnimationName = "Walk";
    std::string RunAnimationName = "Run";

    float IdleThreshold = 0.0f;
    float WalkThreshold = 1.8f;
    float RunThreshold = 5.5f;

    float WalkAuthoredMotionSpeed = 1.8f;
    float RunAuthoredMotionSpeed = 5.5f;
};

// ============================================================================
// HumanoidAnimationProfile
// ============================================================================
// 1体のHumanoid Assetに対するAnimation設定の入口です。
// 今回はLocomotionのみですが、今後Jump / Fall / Landing / UpperBody Layer等を追加しても
// Character側へAsset固有値を戻さず、このProfileを拡張できる構造にします。
struct HumanoidAnimationProfile
{
    HumanoidLocomotionProfile Locomotion{};
};

// Raven_human_test.glb専用の初期設定を返します。
// Asset固有値はこのFactoryへ集約し、CharacterControllerDemoLayerからGLB固有知識を分離します。
inline HumanoidAnimationProfile CreateRavenHumanTestAnimationProfile()
{
    HumanoidAnimationProfile profile{};
    profile.Locomotion.IdleAnimationName = "Idle";
    profile.Locomotion.WalkAnimationName = "Walk";
    profile.Locomotion.RunAnimationName = "Run";
    profile.Locomotion.IdleThreshold = 0.0f;
    profile.Locomotion.WalkThreshold = 1.8f;
    profile.Locomotion.RunThreshold = 5.5f;
    profile.Locomotion.WalkAuthoredMotionSpeed = 1.8f;
    profile.Locomotion.RunAuthoredMotionSpeed = 5.5f;
    return profile;
}

} // namespace Raven
