// Raven/Animation/AnimatorState.h
#pragma once

namespace Raven
{

class AnimationClip;

// ============================================================================
// AnimatorState
// ============================================================================
// Animatorが「現在どのAnimationClipを、どの時刻で再生しているか」を保持する
// 最小のRuntime Stateです。
//
// AnimationClipそのものはAsset/定義データとして共有し、再生時刻などの可変状態を
// Clip側へ持たせません。これにより同じClipを複数Entityが異なる時刻で再生できます。
//
// CrossFadeでは Current / Next の2つのAnimatorStateを同時に進め、それぞれから
// SkeletonPoseをSampleした後にPose同士をBlendする構造へ拡張します。
struct AnimatorState
{
    // 再生対象Clipへの非所有ポインタです。
    // ClipのLifetimeはAnimatorより長いAsset管理側が保証する想定です。
    const AnimationClip* Clip = nullptr;

    // Clip内の現在再生時刻[秒]です。
    // Tickではなく秒をRuntimeの共通単位にすることで、Update(deltaTime)との境界を単純化します。
    float Time = 0.0f;

    // Clip終端へ到達した際に先頭へ戻すかどうかです。
    bool Loop = true;

    bool IsValid() const
    {
        return Clip != nullptr;
    }

    void Reset()
    {
        Clip = nullptr;
        Time = 0.0f;
        Loop = true;
    }
};

} // namespace Raven
