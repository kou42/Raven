#include "Raven/Animation/Animator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Raven
{
namespace
{
TransformPose BlendTransformPose(
    const TransformPose& from,
    const TransformPose& to,
    float weight)
{
    const float t = std::clamp(weight, 0.0f, 1.0f);

    TransformPose result{};

    // Position / Scaleは線形空間なのでLerpします。
    result.Position = math::Vec3::Lerp(from.Position, to.Position, t);
    result.Scale = math::Vec3::Lerp(from.Scale, to.Scale, t);

    // RotationはQuaternionのSlerpを使用します。
    // 行列やEuler角を直接補間しないことで、CrossFade中の回転を自然につなぎます。
    result.Rotation = math::Quat::Slerp(from.Rotation, to.Rotation, t);

    return result;
}
} // namespace

void Animator::Play(std::shared_ptr<AnimationClip> clip, bool restart)
{
    if (!clip)
    {
        Stop();
        m_CurrentState.Reset();
        return;
    }

    // PlayはCrossFadeを中断して指定ClipをCurrentStateへ直接設定します。
    // State Machine側から「即時遷移」を行いたい場合にも同じ経路を利用できます。
    m_CrossFading = false;
    m_NextState.Reset();
    m_FadeElapsed = 0.0f;
    m_FadeDuration = 0.0f;

    const bool clipChanged = (m_CurrentState.Clip != clip);
    m_CurrentState.Clip = std::move(clip);
    m_CurrentState.Loop = m_Loop;

    if (restart || clipChanged)
    {
        // 逆再生の場合は末尾から開始します。
        m_CurrentState.Time =
            (m_Speed < 0.0f) ? m_CurrentState.Clip->GetDuration() : 0.0f;
    }

    m_Playing = true;
    m_Paused = false;
    m_Finished = false;
    EvaluateCurrentPose();
}

bool Animator::CrossFade(std::shared_ptr<AnimationClip> clip, float duration, bool restart)
{
    if (!clip)
    {
        return false;
    }

    // まだCurrent Clipが無い場合、Blend元Poseが存在しないため通常Playへフォールバックします。
    if (!m_CurrentState.IsValid())
    {
        Play(std::move(clip), true);
        return true;
    }

    // Fade中の割り込みは、現在のBlend済みPoseをSnapshot Stateとして保持しない限り
    // Current/Nextどちらを新しいBlend元にするかでPoseが跳ぶ可能性があります。
    // 最初の実装では明示的に拒否し、Idle/Walk/Runの1段Transitionを確実にします。
    if (m_CrossFading)
    {
        return false;
    }

    // duration <= 0 は即時遷移として扱います。
    if (duration <= 0.0f)
    {
        Play(std::move(clip), restart);
        return true;
    }

    // 同じClipへrestart=falseで遷移する場合は見た目が変化しないため何もしません。
    if (m_CurrentState.Clip == clip && !restart)
    {
        return true;
    }

    m_NextState.Clip = std::move(clip);
    m_NextState.Loop = m_Loop;
    m_NextState.Time =
        (restart || m_NextState.Clip != m_CurrentState.Clip)
            ? ((m_Speed < 0.0f) ? m_NextState.Clip->GetDuration() : 0.0f)
            : m_CurrentState.Time;

    m_CrossFading = true;
    m_FadeElapsed = 0.0f;
    m_FadeDuration = duration;

    // Pause中にCrossFadeを予約した場合は、Resume後から時間が進みます。
    // Stop状態からCrossFadeした場合は遷移を開始できるようPlayingへ戻します。
    m_Playing = true;
    m_Finished = false;

    EvaluateCurrentPose();
    return true;
}

void Animator::Pause()
{
    if (!m_Playing)
    {
        return;
    }

    m_Paused = true;
}

void Animator::Resume()
{
    if (!m_Playing || !m_CurrentState.IsValid())
    {
        return;
    }

    m_Paused = false;
}

void Animator::Stop()
{
    // StopはClip参照を維持したまま先頭へ戻します。
    // CrossFadeだけは破棄し、停止後のPoseが曖昧にならないようCurrent側へ一本化します。
    m_CurrentState.Time = 0.0f;
    m_NextState.Reset();
    m_CrossFading = false;
    m_FadeElapsed = 0.0f;
    m_FadeDuration = 0.0f;

    m_Playing = false;
    m_Paused = false;
    m_Finished = false;
    EvaluateCurrentPose();
}

void Animator::SetLoop(bool loop)
{
    m_Loop = loop;
    m_CurrentState.Loop = loop;

    if (m_NextState.IsValid())
    {
        m_NextState.Loop = loop;
    }
}

void Animator::SetCurrentTime(float time)
{
    if (!m_CurrentState.IsValid())
    {
        m_CurrentState.Time = 0.0f;
        m_CurrentPose = TransformPose{};

        if (m_Skeleton)
        {
            m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        }
        return;
    }

    // Timeline ScrubはCurrent Stateに対する明示操作なのでCrossFadeを解除します。
    m_NextState.Reset();
    m_CrossFading = false;
    m_FadeElapsed = 0.0f;
    m_FadeDuration = 0.0f;

    m_CurrentState.Time = std::clamp(
        time,
        0.0f,
        m_CurrentState.Clip->GetDuration());

    m_Finished = false;
    EvaluateCurrentPose();
}

void Animator::SetNormalizedTime(float normalizedTime)
{
    if (!m_CurrentState.IsValid() || m_CurrentState.Clip->GetDuration() <= 0.0f)
    {
        SetCurrentTime(0.0f);
        return;
    }

    const float normalized = std::clamp(normalizedTime, 0.0f, 1.0f);
    SetCurrentTime(normalized * m_CurrentState.Clip->GetDuration());
}

float Animator::GetNormalizedTime() const
{
    if (!m_CurrentState.IsValid() || m_CurrentState.Clip->GetDuration() <= 0.0f)
    {
        return 0.0f;
    }

    return std::clamp(
        m_CurrentState.Time / m_CurrentState.Clip->GetDuration(),
        0.0f,
        1.0f);
}

float Animator::GetCrossFadeWeight() const
{
    if (!m_CrossFading)
    {
        return 0.0f;
    }

    if (m_FadeDuration <= 0.0f)
    {
        return 1.0f;
    }

    return std::clamp(m_FadeElapsed / m_FadeDuration, 0.0f, 1.0f);
}

void Animator::SetSkeleton(const Skeleton* skeleton)
{
    m_Skeleton = skeleton;

    if (!m_Skeleton)
    {
        // SkeletonPoseは内部vectorだけを持つため明示的なClear APIは不要です。
        // Skeleton未接続時はGetCurrentSkeletonPose()をSkinningへ渡さないことを契約とします。
        m_CurrentSkeletonPose = SkeletonPose{};
        m_NextSkeletonPose = SkeletonPose{};
        return;
    }

    // Skeletonを接続した瞬間から有効なPoseを返せるよう、まずBind Poseを構築します。
    // Current Clipが既に再生中なら、その直後に現在時刻で再評価します。
    m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
    m_NextSkeletonPose.ResetToBindPose(*m_Skeleton);
    EvaluateCurrentPose();
}

void Animator::Update(float dt)
{
    if (!m_Playing || m_Paused || !m_CurrentState.IsValid())
    {
        return;
    }

    const bool currentFinished = AdvanceState(m_CurrentState, dt);

    if (!m_CrossFading)
    {
        if (currentFinished)
        {
            m_Playing = false;
            m_Finished = true;
        }
        else
        {
            m_Finished = false;
        }

        EvaluateCurrentPose();
        return;
    }

    const bool nextFinished = AdvanceState(m_NextState, dt);

    // Fade時間はAnimation Speedとは独立した「遷移に掛ける実時間」として進めます。
    // Speed=0でもCrossFadeそのものは完了でき、負SpeedでもFade Weightは逆行しません。
    m_FadeElapsed += std::max(dt, 0.0f);

    EvaluateCurrentPose();

    if (GetCrossFadeWeight() >= 1.0f)
    {
        CompleteCrossFade();

        if (nextFinished)
        {
            m_Playing = false;
            m_Finished = true;
        }
    }
}

bool Animator::AdvanceState(AnimatorState& state, float dt)
{
    if (!state.IsValid())
    {
        return false;
    }

    const float duration = state.Clip->GetDuration();

    if (duration <= 0.0f)
    {
        state.Time = 0.0f;
        return !state.Loop;
    }

    state.Time += dt * m_Speed;

    if (state.Loop)
    {
        state.Time = std::fmod(state.Time, duration);
        if (state.Time < 0.0f)
        {
            state.Time += duration;
        }

        return false;
    }

    if (state.Time >= duration)
    {
        state.Time = duration;
        return true;
    }

    if (state.Time <= 0.0f && m_Speed < 0.0f)
    {
        state.Time = 0.0f;
        return true;
    }

    state.Time = std::clamp(state.Time, 0.0f, duration);
    return false;
}

void Animator::EvaluateCurrentPose()
{
    if (!m_CurrentState.IsValid())
    {
        m_CurrentPose = TransformPose{};

        if (m_Skeleton)
        {
            m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        }
        return;
    }

    // ------------------------------------------------------------------------
    // 単一Transform Animation
    // ------------------------------------------------------------------------
    // 既存Scene/ECS Animation経路は従来どおり維持します。
    const TransformPose currentPose =
        m_CurrentState.Clip->Sample(m_CurrentState.Time);

    if (!m_CrossFading || !m_NextState.IsValid())
    {
        m_CurrentPose = currentPose;
    }
    else
    {
        const TransformPose nextPose =
            m_NextState.Clip->Sample(m_NextState.Time);

        m_CurrentPose = BlendTransformPose(
            currentPose,
            nextPose,
            GetCrossFadeWeight());
    }

    // ------------------------------------------------------------------------
    // Skeletal Animation
    // ------------------------------------------------------------------------
    // Skeleton未接続のAnimatorはここで終了します。
    // これにより従来のTransform Animation利用側へ追加コストを掛けません。
    if (!m_Skeleton)
    {
        return;
    }

    if (!m_CrossFading || !m_NextState.IsValid())
    {
        // 単一State時はClipから最終出力Poseへ直接Sampleします。
        // AnimationClip::Sample()はTrackの無いBoneをBind Poseのまま維持します。
        if (!m_CurrentState.Clip->Sample(
                *m_Skeleton,
                m_CurrentState.Time,
                m_CurrentSkeletonPose))
        {
            // SkeletonとClipのBone対応が不正な場合、壊れたPoseをSkinningへ渡さないため
            // Bind Poseへ戻します。将来はError/Assert経路へ接続できます。
            m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        }
        return;
    }

    // CrossFade中はCurrent/Nextを別々のSkeletonPoseへSampleした後、Local TRSをBlendします。
    // Global行列同士を直接Lerpしないことが重要です。行列補間では回転やScaleが歪むため、
    // BlendPoses()内でTranslation/Scale=Lerp、Rotation=SlerpしてからGlobalを再構築します。
    SkeletonPose currentSamplePose;
    if (!m_CurrentState.Clip->Sample(
            *m_Skeleton,
            m_CurrentState.Time,
            currentSamplePose))
    {
        m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        return;
    }

    if (!m_NextState.Clip->Sample(
            *m_Skeleton,
            m_NextState.Time,
            m_NextSkeletonPose))
    {
        m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        return;
    }

    if (!BlendPoses(
            *m_Skeleton,
            currentSamplePose,
            m_NextSkeletonPose,
            GetCrossFadeWeight(),
            m_CurrentSkeletonPose))
    {
        m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
    }
}

void Animator::CompleteCrossFade()
{
    if (!m_CrossFading || !m_NextState.IsValid())
    {
        return;
    }

    // Next StateをそのままCurrentへ昇格するため、遷移完了時に再生時刻は途切れません。
    m_CurrentState = std::move(m_NextState);
    m_NextState.Reset();

    m_CrossFading = false;
    m_FadeElapsed = 0.0f;
    m_FadeDuration = 0.0f;

    // EvaluateCurrentPose()はFade Weight=1の状態で直前に評価済みですが、
    // CurrentState一本になった後も状態と出力Poseを同期させておきます。
    EvaluateCurrentPose();
}

} // namespace Raven
