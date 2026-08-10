// Raven/Animation/AnimatorState.h
#pragma once

#include <memory>

namespace Raven
{

class AnimationClip;

// ============================================================================
// AnimatorState
// ============================================================================
// Animatorが「どのClipを、どの時刻で再生しているか」を表すRuntime Stateです。
//
// CrossFadeではCurrentStateとNextStateを同時に進める必要があります。
// ClipとTimeを別々のAnimatorメンバとして持つより、1つのStateへまとめておくことで
// 「現在側」「遷移先側」を同じロジックで更新・評価できます。
struct AnimatorState
{
    // AnimationClipはAssetとして複数Animatorから共有されるためshared_ptrで保持します。
    // CrossFade中もCurrent / Next双方のClip Lifetimeを安全に維持できます。
    std::shared_ptr<AnimationClip> Clip = nullptr;

    // Clip内の現在再生時刻[秒]です。
    float Time = 0.0f;

    // Clip終端へ到達した際に先頭へ戻すかどうかです。
    bool Loop = true;

    bool IsValid() const
    {
        return Clip != nullptr;
    }

    void Reset()
    {
        Clip.reset();
        Time = 0.0f;
        Loop = true;
    }
};

} // namespace Raven
