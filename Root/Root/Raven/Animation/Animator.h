#pragma once

#include "Raven/Animation/AnimationClip.h"

#include <memory>

namespace Raven
{

// ============================================================================
// Animator
// ============================================================================
// AnimationClipが「再生されるデータ」であるのに対し、Animatorは
// 「そのClipを現在どのように再生しているか」というruntime stateを所有します。
//
// Clipをshared_ptrで参照する理由:
//   同じWalk/IdleなどのClip Assetを複数Entityが共有しながら、Animatorごとに
//   CurrentTime / Speed / Loop / Playingを独立して持てるようにするためです。
class Animator
{
public:
    void Play(std::shared_ptr<AnimationClip> clip, bool restart = true);
    void Pause();
    void Resume();
    void Stop();

    void Update(float dt);

    void SetLoop(bool loop) { m_Loop = loop; }
    bool IsLooping() const { return m_Loop; }

    void SetSpeed(float speed) { m_Speed = speed; }
    float GetSpeed() const { return m_Speed; }

    bool IsPlaying() const { return m_Playing; }
    bool IsPaused() const { return m_Paused; }

    float GetCurrentTime() const { return m_CurrentTime; }
    const TransformPose& GetCurrentPose() const { return m_CurrentPose; }
    const std::shared_ptr<AnimationClip>& GetClip() const { return m_Clip; }

private:
    // CurrentTimeをClipの再生範囲へ正規化し、その時刻のPoseを再評価します。
    void EvaluateCurrentPose();

private:
    std::shared_ptr<AnimationClip> m_Clip = nullptr;

    float m_CurrentTime = 0.0f;
    float m_Speed = 1.0f;

    bool m_Playing = false;
    bool m_Paused = false;
    bool m_Loop = true;

    TransformPose m_CurrentPose{};
};

} // namespace Raven
