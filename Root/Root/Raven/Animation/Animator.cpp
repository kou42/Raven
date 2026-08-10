#include "Raven/Animation/Animator.h"

#include <algorithm>
#include <cmath>

namespace Raven
{

void Animator::Play(std::shared_ptr<AnimationClip> clip, bool restart)
{
    if (!clip)
    {
        Stop();
        m_Clip.reset();
        return;
    }

    // 同じClipをrestart=falseでPlayした場合は現在位置を維持します。
    // Clipが切り替わった場合は、別Animationの時刻を引き継ぐ意味がないため
    // restart指定に関係なく先頭から開始します。
    const bool clipChanged = (m_Clip != clip);
    m_Clip = std::move(clip);

    if (restart || clipChanged)
    {
        // 逆再生の場合は末尾から開始した方が自然です。
        // これにより SetSpeed(-1) -> Play(clip) だけで逆方向再生できます。
        m_CurrentTime = (m_Speed < 0.0f) ? m_Clip->GetDuration() : 0.0f;
    }

    m_Playing = true;
    m_Paused = false;
    m_Finished = false;
    EvaluateCurrentPose();
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
    if (!m_Playing || !m_Clip)
    {
        return;
    }

    m_Paused = false;
}

void Animator::Stop()
{
    // Stopは「再生終了」とは区別します。
    // Finishedをfalseへ戻すことで、State Machineが手動StopをClip完走と誤認しません。
    m_CurrentTime = 0.0f;
    m_Playing = false;
    m_Paused = false;
    m_Finished = false;
    EvaluateCurrentPose();
}

void Animator::SetCurrentTime(float time)
{
    if (!m_Clip)
    {
        m_CurrentTime = 0.0f;
        m_CurrentPose = TransformPose{};
        return;
    }

    m_CurrentTime = std::clamp(time, 0.0f, m_Clip->GetDuration());
    m_Finished = false;
    EvaluateCurrentPose();
}

void Animator::SetNormalizedTime(float normalizedTime)
{
    if (!m_Clip || m_Clip->GetDuration() <= 0.0f)
    {
        SetCurrentTime(0.0f);
        return;
    }

    // normalizedTimeはEditor/Gameplay APIとして扱いやすい[0,1]へClampします。
    const float normalized = std::clamp(normalizedTime, 0.0f, 1.0f);
    SetCurrentTime(normalized * m_Clip->GetDuration());
}

float Animator::GetNormalizedTime() const
{
    if (!m_Clip || m_Clip->GetDuration() <= 0.0f)
    {
        return 0.0f;
    }

    return std::clamp(m_CurrentTime / m_Clip->GetDuration(), 0.0f, 1.0f);
}

void Animator::Update(float dt)
{
    if (!m_Playing || m_Paused || !m_Clip)
    {
        return;
    }

    const float duration = m_Clip->GetDuration();

    // Duration=0のClipは時間を進められません。
    // Sample(0)だけ評価して完了扱いにすることでfmodの0除算も防ぎます。
    if (duration <= 0.0f)
    {
        m_CurrentTime = 0.0f;
        m_CurrentPose = m_Clip->Sample(0.0f);
        m_Playing = false;
        m_Finished = true;
        return;
    }

    m_CurrentTime += dt * m_Speed;

    if (m_Loop)
    {
        // fmodは負の入力に対して負値を返すため、逆再生(speed < 0)でも
        // [0, duration)へ収まるように補正します。
        m_CurrentTime = std::fmod(m_CurrentTime, duration);
        if (m_CurrentTime < 0.0f)
        {
            m_CurrentTime += duration;
        }

        // Loop Animationは端へ到達しても完了状態にはなりません。
        m_Finished = false;
    }
    else
    {
        // 非Loop再生ではClip端へ到達した時点で停止し、Finishedを立てます。
        // 後続のAnimator State MachineではこのFlagをExit Time相当の条件として利用できます。
        if (m_CurrentTime >= duration)
        {
            m_CurrentTime = duration;
            m_Playing = false;
            m_Finished = true;
        }
        else if (m_CurrentTime <= 0.0f && m_Speed < 0.0f)
        {
            m_CurrentTime = 0.0f;
            m_Playing = false;
            m_Finished = true;
        }
    }

    EvaluateCurrentPose();
}

void Animator::EvaluateCurrentPose()
{
    if (!m_Clip)
    {
        m_CurrentPose = TransformPose{};
        return;
    }

    const float duration = m_Clip->GetDuration();
    m_CurrentTime = std::clamp(m_CurrentTime, 0.0f, duration);
    m_CurrentPose = m_Clip->Sample(m_CurrentTime);
}

} // namespace Raven
