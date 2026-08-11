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

    // 同じClipかどうかだけではなく、BlendTreeからClipへ切り替わるケースもMotion変更として扱います。
    // restart=falseでもMotion種類が変わる場合は旧Motionの位相をそのまま流用せず、再生開始位置を初期化します。
    const bool motionChanged =
        m_CurrentState.MotionType != AnimatorMotionType::Clip ||
        m_CurrentState.Clip != clip;

    m_CurrentState.MotionType = AnimatorMotionType::Clip;
    m_CurrentState.Clip = std::move(clip);
    m_CurrentState.BlendTree.reset();
    m_CurrentState.BlendParameter = 0.0f;
    m_CurrentState.Loop = m_Loop;

    if (restart || motionChanged)
    {
        // 逆再生の場合は末尾から開始します。
        m_CurrentState.NormalizedTime = (m_Speed < 0.0f) ? 1.0f : 0.0f;
    }

    m_CurrentState.Time =
        m_CurrentState.NormalizedTime * GetStateDuration(m_CurrentState);

    m_Playing = true;
    m_Paused = false;
    m_Finished = false;
    EvaluateCurrentPose();
}

void Animator::PlayBlendTree(
    std::shared_ptr<BlendTree1D> blendTree,
    float blendParameter,
    bool restart)
{
    if (!blendTree || !std::isfinite(blendParameter) || blendTree->GetChildCount() == 0)
    {
        Stop();
        m_CurrentState.Reset();
        return;
    }

    // Clip版Playと同じく、直接再生は進行中CrossFadeを破棄してCurrent Motionへ一本化します。
    // Blend TreeのParameter値はAssetではなくAnimatorStateのRuntime値として保持します。
    m_CrossFading = false;
    m_NextState.Reset();
    m_FadeElapsed = 0.0f;
    m_FadeDuration = 0.0f;

    const bool motionChanged =
        m_CurrentState.MotionType != AnimatorMotionType::BlendTree1D ||
        m_CurrentState.BlendTree != blendTree;

    m_CurrentState.MotionType = AnimatorMotionType::BlendTree1D;
    m_CurrentState.Clip.reset();
    m_CurrentState.BlendTree = std::move(blendTree);
    m_CurrentState.BlendParameter = blendParameter;
    m_CurrentState.Loop = m_Loop;

    if (restart || motionChanged)
    {
        m_CurrentState.NormalizedTime = (m_Speed < 0.0f) ? 1.0f : 0.0f;
    }

    // Blend TreeではParameter位置に応じて補間Durationが変わるため、TimeはNormalizedTimeから再計算します。
    // 位相を正規値として保持することでSpeed変更時にも歩行周期を連続させます。
    m_CurrentState.Time =
        m_CurrentState.NormalizedTime * GetStateDuration(m_CurrentState);

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

    // まだCurrent Motionが無い場合、Blend元Poseが存在しないため通常Playへフォールバックします。
    if (!m_CurrentState.IsValid())
    {
        Play(std::move(clip), true);
        return true;
    }

    // Fade中の割り込みは、現在のBlend済みPoseをSnapshot Stateとして保持しない限り
    // Current/Nextどちらを新しいBlend元にするかでPoseが跳ぶ可能性があります。
    // 現段階では明示的に拒否し、Snapshot Pose導入後にInterruptポリシーとして拡張します。
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
    if (m_CurrentState.MotionType == AnimatorMotionType::Clip &&
        m_CurrentState.Clip == clip &&
        !restart)
    {
        return true;
    }

    m_NextState.Reset();
    m_NextState.MotionType = AnimatorMotionType::Clip;
    m_NextState.Clip = std::move(clip);
    m_NextState.Loop = m_Loop;

    // restart=falseではCurrent MotionのNormalizedTimeを引き継ぎます。
    // Clip長が異なっても同じ位相から遷移先を始められるため、同期Motionで利用できます。
    m_NextState.NormalizedTime =
        restart ? ((m_Speed < 0.0f) ? 1.0f : 0.0f)
                : m_CurrentState.NormalizedTime;
    m_NextState.Time =
        m_NextState.NormalizedTime * GetStateDuration(m_NextState);

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

bool Animator::CrossFadeBlendTree(
    std::shared_ptr<BlendTree1D> blendTree,
    float blendParameter,
    float duration,
    bool restart)
{
    if (!blendTree || !std::isfinite(blendParameter) || blendTree->GetChildCount() == 0)
    {
        return false;
    }

    // Current Motionが無ければBlend元Poseが存在しないため、Clip版と同じく直接Playへ落とします。
    if (!m_CurrentState.IsValid())
    {
        PlayBlendTree(std::move(blendTree), blendParameter, true);
        return true;
    }

    // BlendTree遷移もClip遷移と同じInterrupt制約を持ちます。
    // Current/Nextの途中Blend PoseをSnapshotとして保持できるまでは再CrossFadeを拒否します。
    if (m_CrossFading)
    {
        return false;
    }

    if (duration <= 0.0f)
    {
        PlayBlendTree(std::move(blendTree), blendParameter, restart);
        return true;
    }

    // 同じBlendTreeへrestart=falseで要求された場合はState遷移を作らず、Parameter値だけ更新します。
    // これによりSpeedが毎Frame変わってもLocomotion周期をリスタートしません。
    if (m_CurrentState.MotionType == AnimatorMotionType::BlendTree1D &&
        m_CurrentState.BlendTree == blendTree &&
        !restart)
    {
        m_CurrentState.BlendParameter = blendParameter;
        m_CurrentState.Time =
            m_CurrentState.NormalizedTime * GetStateDuration(m_CurrentState);
        EvaluateCurrentPose();
        return true;
    }

    m_NextState.Reset();
    m_NextState.MotionType = AnimatorMotionType::BlendTree1D;
    m_NextState.BlendTree = std::move(blendTree);
    m_NextState.BlendParameter = blendParameter;
    m_NextState.Loop = m_Loop;
    m_NextState.NormalizedTime =
        restart ? ((m_Speed < 0.0f) ? 1.0f : 0.0f)
                : m_CurrentState.NormalizedTime;
    m_NextState.Time =
        m_NextState.NormalizedTime * GetStateDuration(m_NextState);

    m_CrossFading = true;
    m_FadeElapsed = 0.0f;
    m_FadeDuration = duration;
    m_Playing = true;
    m_Finished = false;

    EvaluateCurrentPose();
    return true;
}

bool Animator::SetCurrentBlendParameter(float value)
{
    if (!std::isfinite(value) ||
        m_CurrentState.MotionType != AnimatorMotionType::BlendTree1D ||
        !m_CurrentState.BlendTree)
    {
        return false;
    }

    // Parameter変更で補間Durationが変わってもNormalizedTimeは変更しません。
    // Timeだけを新Durationから再構成し、歩行周期の位相を保ったままPoseを再評価します。
    m_CurrentState.BlendParameter = value;
    m_CurrentState.Time =
        m_CurrentState.NormalizedTime * GetStateDuration(m_CurrentState);
    EvaluateCurrentPose();
    return true;
}

bool Animator::SetNextBlendParameter(float value)
{
    if (!std::isfinite(value) ||
        m_NextState.MotionType != AnimatorMotionType::BlendTree1D ||
        !m_NextState.BlendTree)
    {
        return false;
    }

    // CrossFade先もCurrentと同じく位相を維持したままParameterだけ更新します。
    // Land -> Locomotion Fade中にSpeedが変化した場合でも遷移先Poseを最新入力へ追従させられます。
    m_NextState.BlendParameter = value;
    m_NextState.Time =
        m_NextState.NormalizedTime * GetStateDuration(m_NextState);
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
    // StopはMotion参照を維持したまま先頭へ戻します。
    // CrossFadeだけは破棄し、停止後のPoseが曖昧にならないようCurrent側へ一本化します。
    m_CurrentState.Time = 0.0f;
    m_CurrentState.NormalizedTime = 0.0f;
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
        m_CurrentState.NormalizedTime = 0.0f;
        m_CurrentPose = TransformPose{};

        if (m_Skeleton)
        {
            m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        }
        return;
    }

    // Timeline ScrubはCurrent Stateに対する明示操作なのでCrossFadeを解除します。
    // Next StateとFade時間を残すと、Editorで指定したCurrent時刻とBlend結果が一致しなくなるためです。
    m_NextState.Reset();
    m_CrossFading = false;
    m_FadeElapsed = 0.0f;
    m_FadeDuration = 0.0f;

    const float duration = GetStateDuration(m_CurrentState);
    if (duration <= 0.0f)
    {
        m_CurrentState.Time = 0.0f;
        m_CurrentState.NormalizedTime = 0.0f;
    }
    else
    {
        m_CurrentState.Time = std::clamp(time, 0.0f, duration);
        m_CurrentState.NormalizedTime =
            std::clamp(m_CurrentState.Time / duration, 0.0f, 1.0f);
    }

    m_Finished = false;
    EvaluateCurrentPose();
}

void Animator::SetNormalizedTime(float normalizedTime)
{
    if (!m_CurrentState.IsValid())
    {
        SetCurrentTime(0.0f);
        return;
    }

    // SetCurrentTime()と同じく、Timelineの明示操作はCurrent Motion単体へ戻します。
    // BlendTreeでもNormalizedTimeが正規の位相なので、その値から現在Durationに対応する実時間を再構成します。
    m_NextState.Reset();
    m_CrossFading = false;
    m_FadeElapsed = 0.0f;
    m_FadeDuration = 0.0f;

    m_CurrentState.NormalizedTime =
        std::clamp(normalizedTime, 0.0f, 1.0f);
    m_CurrentState.Time =
        m_CurrentState.NormalizedTime * GetStateDuration(m_CurrentState);

    m_Finished = false;
    EvaluateCurrentPose();
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
    // Current Motionが既に再生中なら、その直後に現在時刻/位相で再評価します。
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

float Animator::GetStateDuration(const AnimatorState& state) const
{
    if (!state.IsValid())
    {
        return 0.0f;
    }

    if (state.MotionType == AnimatorMotionType::Clip)
    {
        return state.Clip ? std::max(state.Clip->GetDuration(), 0.0f) : 0.0f;
    }

    if (state.MotionType == AnimatorMotionType::BlendTree1D && state.BlendTree)
    {
        float duration = 0.0f;
        if (state.BlendTree->GetBlendedDuration(state.BlendParameter, duration))
        {
            return std::max(duration, 0.0f);
        }
    }

    return 0.0f;
}

bool Animator::AdvanceState(AnimatorState& state, float dt)
{
    if (!state.IsValid())
    {
        return false;
    }

    const float duration = GetStateDuration(state);

    if (duration <= 0.0f)
    {
        state.Time = 0.0f;
        state.NormalizedTime = 0.0f;
        return !state.Loop;
    }

    // ClipとBlendTreeの両方をNormalized Timeで進めます。
    // BlendTreeはParameter変化で補間Durationが変わっても位相を保持できることが重要です。
    state.NormalizedTime += (dt * m_Speed) / duration;

    if (state.Loop)
    {
        state.NormalizedTime = std::fmod(state.NormalizedTime, 1.0f);
        if (state.NormalizedTime < 0.0f)
        {
            state.NormalizedTime += 1.0f;
        }

        state.Time = state.NormalizedTime * duration;
        return false;
    }

    if (state.NormalizedTime >= 1.0f)
    {
        state.NormalizedTime = 1.0f;
        state.Time = duration;
        return true;
    }

    if (state.NormalizedTime <= 0.0f && m_Speed < 0.0f)
    {
        state.NormalizedTime = 0.0f;
        state.Time = 0.0f;
        return true;
    }

    state.NormalizedTime = std::clamp(state.NormalizedTime, 0.0f, 1.0f);
    state.Time = state.NormalizedTime * duration;
    return false;
}

bool Animator::SampleTransformPose(const AnimatorState& state, TransformPose& outPose) const
{
    if (!state.IsValid())
    {
        return false;
    }

    if (state.MotionType == AnimatorMotionType::Clip && state.Clip)
    {
        outPose = state.Clip->Sample(state.Time);
        return true;
    }

    if (state.MotionType == AnimatorMotionType::BlendTree1D && state.BlendTree)
    {
        return state.BlendTree->SampleTransform(
            state.BlendParameter,
            state.NormalizedTime,
            outPose);
    }

    return false;
}

bool Animator::SampleSkeletonPose(const AnimatorState& state, SkeletonPose& outPose) const
{
    if (!m_Skeleton || !state.IsValid())
    {
        return false;
    }

    if (state.MotionType == AnimatorMotionType::Clip && state.Clip)
    {
        return state.Clip->Sample(*m_Skeleton, state.Time, outPose);
    }

    if (state.MotionType == AnimatorMotionType::BlendTree1D && state.BlendTree)
    {
        return state.BlendTree->SampleSkeleton(
            *m_Skeleton,
            state.BlendParameter,
            state.NormalizedTime,
            outPose);
    }

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
    // 既存Scene/ECS Animation経路は従来どおり維持し、Motion種類だけSample関数内で吸収します。
    TransformPose currentPose{};
    if (!SampleTransformPose(m_CurrentState, currentPose))
    {
        m_CurrentPose = TransformPose{};
        return;
    }

    if (!m_CrossFading || !m_NextState.IsValid())
    {
        m_CurrentPose = currentPose;
    }
    else
    {
        TransformPose nextPose{};
        if (!SampleTransformPose(m_NextState, nextPose))
        {
            m_CurrentPose = currentPose;
        }
        else
        {
            // Clip / BlendTreeの種類に関係なく最終Pose同士を補間します。
            // これによりLocomotion Blend Tree -> JumpStart Clipでも遷移開始時のPoseが跳びません。
            m_CurrentPose = BlendTransformPose(
                currentPose,
                nextPose,
                GetCrossFadeWeight());
        }
    }

    // ------------------------------------------------------------------------
    // Skeletal Animation
    // ------------------------------------------------------------------------
    // Skeleton未接続のAnimatorはここで終了します。
    // これにより従来のTransform Animation利用側へ追加のSkeleton Sampleコストを掛けません。
    if (!m_Skeleton)
    {
        return;
    }

    if (!m_CrossFading || !m_NextState.IsValid())
    {
        // 単一State時はCurrent Motionから最終出力Poseへ直接Sampleします。
        // Clipの場合は未指定BoneをBind Poseのまま維持し、BlendTreeの場合はChild Poseを補間します。
        if (!SampleSkeletonPose(m_CurrentState, m_CurrentSkeletonPose))
        {
            // Motion/Skeleton対応が不正な場合、壊れたPoseをSkinningへ渡さないためBind Poseへ戻します。
            m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        }
        return;
    }

    // CrossFade中はCurrent/Next Motionを別々のSkeletonPoseへSampleした後、Local TRSをBlendします。
    SkeletonPose currentSamplePose;
    if (!SampleSkeletonPose(m_CurrentState, currentSamplePose))
    {
        m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        return;
    }

    if (!SampleSkeletonPose(m_NextState, m_NextSkeletonPose))
    {
        m_CurrentSkeletonPose.ResetToBindPose(*m_Skeleton);
        return;
    }

    // Global行列同士を直接Lerpしないことが重要です。行列補間では回転やScaleが歪むため、
    // BlendPoses()内でTranslation/Scale=Lerp、Rotation=SlerpしてからGlobalを再構築します。
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

    // Next StateをそのままCurrentへ昇格するため、遷移完了時に再生位相は途切れません。
    // Motion種類やBlendParameterもAnimatorStateごと引き継ぐため、Clip/BlendTreeで分岐する必要はありません。
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
