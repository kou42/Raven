// Raven/Animation/AnimatorState.h
#pragma once

#include <memory>

namespace Raven
{

class AnimationClip;
class BlendTree1D;

// ============================================================================
// AnimatorMotionType
// ============================================================================
// AnimatorStateが現在どの種類のAnimation Motionを再生しているかを表します。
// ClipとBlendTreeを同じCurrent / Next State経路で扱うことで、両者間のCrossFadeも
// Animatorの既存2-State Blend処理へ統合できます。
enum class AnimatorMotionType
{
    None,
    Clip,
    BlendTree1D
};

// ============================================================================
// AnimatorState
// ============================================================================
// Animatorが「どのMotionを、どの時刻/位相で再生しているか」を表すRuntime Stateです。
//
// CrossFadeではCurrentStateとNextStateを同時に進める必要があります。
// Motion参照と時間を1つのStateへまとめることで、AnimationClipとBlendTree1Dのどちらでも
// 「現在側」「遷移先側」を同じロジックで更新・評価できます。
struct AnimatorState
{
    AnimatorMotionType MotionType = AnimatorMotionType::None;

    // Clip Motion。従来の単一AnimationClip再生との互換性を維持します。
    std::shared_ptr<AnimationClip> Clip = nullptr;

    // BlendTree Motion。複数ClipからParameter値に応じてPoseを生成します。
    std::shared_ptr<BlendTree1D> BlendTree = nullptr;

    // BlendTree1Dが参照する現在のFloat値です。
    // Parameter自体はState Machineが所有し、Animatorには評価に必要な値だけ同期します。
    float BlendParameter = 0.0f;

    // Clip MotionではClip内の現在再生時刻[秒]です。
    // BlendTree Motionでは現在Parameterでの補間DurationにNormalizedTimeを掛けた参考秒数です。
    float Time = 0.0f;

    // BlendTreeではClip長が変化しても歩行位相を維持する必要があるため、
    // 0.0～1.0の再生位相を独立して保持します。Clip MotionでもTimeから同期します。
    float NormalizedTime = 0.0f;

    // Motion終端へ到達した際に先頭へ戻すかどうかです。
    bool Loop = true;

    bool IsValid() const
    {
        if (MotionType == AnimatorMotionType::Clip)
        {
            return Clip != nullptr;
        }

        if (MotionType == AnimatorMotionType::BlendTree1D)
        {
            return BlendTree != nullptr;
        }

        return false;
    }

    void Reset()
    {
        MotionType = AnimatorMotionType::None;
        Clip.reset();
        BlendTree.reset();
        BlendParameter = 0.0f;
        Time = 0.0f;
        NormalizedTime = 0.0f;
        Loop = true;
    }
};

} // namespace Raven
