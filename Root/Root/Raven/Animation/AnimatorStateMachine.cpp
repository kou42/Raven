// Raven/Animation/AnimatorStateMachine.cpp
#include "Raven/Animation/AnimatorStateMachine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Raven
{

bool AnimatorStateMachine::AddState(
    std::string name,
    std::shared_ptr<AnimationClip> clip,
    float crossFadeDuration,
    bool restartOnEnter)
{
    if (name.empty() || !clip)
    {
        return false;
    }

    if (m_States.find(name) != m_States.end())
    {
        return false;
    }

    AnimatorStateDefinition state{};
    state.Clip = std::move(clip);
    state.CrossFadeDuration = std::max(crossFadeDuration, 0.0f);
    state.RestartOnEnter = restartOnEnter;

    m_States.emplace(std::move(name), std::move(state));
    return true;
}

bool AnimatorStateMachine::RemoveState(const std::string& name)
{
    const auto it = m_States.find(name);
    if (it == m_States.end())
    {
        return false;
    }

    if (m_CurrentStateName == name)
    {
        m_CurrentStateName.clear();
    }

    if (m_PendingStateName == name)
    {
        m_PendingStateName.clear();
    }

    if (m_QueuedStateName == name)
    {
        m_QueuedStateName.clear();
    }

    m_States.erase(it);
    return true;
}

bool AnimatorStateMachine::HasState(const std::string& name) const
{
    return m_States.find(name) != m_States.end();
}

const AnimatorStateDefinition* AnimatorStateMachine::FindState(const std::string& name) const
{
    const auto it = m_States.find(name);
    return (it != m_States.end()) ? &it->second : nullptr;
}

bool AnimatorStateMachine::SetInitialState(
    const std::string& name,
    bool startImmediately)
{
    const AnimatorStateDefinition* state = FindState(name);
    if (!state || !state->Clip)
    {
        return false;
    }

    m_CurrentStateName = name;
    m_PendingStateName.clear();
    m_QueuedStateName.clear();

    if (startImmediately)
    {
        m_Animator.Play(state->Clip, true);
    }

    return true;
}

bool AnimatorStateMachine::TransitionTo(
    const std::string& name,
    float durationOverride)
{
    const AnimatorStateDefinition* target = FindState(name);
    if (!target || !target->Clip)
    {
        return false;
    }

    if (m_CurrentStateName == name && m_PendingStateName.empty())
    {
        return true;
    }

    if (m_PendingStateName == name)
    {
        return true;
    }

    const float duration =
        (durationOverride >= 0.0f)
            ? durationOverride
            : target->CrossFadeDuration;

    if (!HasCurrentState() || !m_Animator.GetCurrentState().IsValid())
    {
        m_Animator.Play(target->Clip, target->RestartOnEnter);
        m_CurrentStateName = name;
        m_PendingStateName.clear();
        return true;
    }

    if (!m_Animator.CrossFade(
            target->Clip,
            std::max(duration, 0.0f),
            target->RestartOnEnter))
    {
        return false;
    }

    if (!m_Animator.IsCrossFading())
    {
        m_CurrentStateName = name;
        m_PendingStateName.clear();
        return true;
    }

    m_PendingStateName = name;
    return true;
}

bool AnimatorStateMachine::UpdateLocomotion(
    float speed,
    const LocomotionThresholds& thresholds,
    const LocomotionStateNames& stateNames)
{
    // Velocityの向きではなく移動量だけを使うため負値も絶対値へ正規化します。
    // NaN/Infは比較結果が不定になり得るので、入力異常として遷移要求を拒否します。
    if (!std::isfinite(speed))
    {
        return false;
    }

    const float locomotionSpeed = std::abs(speed);
    const float idleMaxSpeed = std::max(thresholds.IdleMaxSpeed, 0.0f);
    const float runMinSpeed = std::max(thresholds.RunMinSpeed, idleMaxSpeed);

    const std::string* targetState = nullptr;

    if (locomotionSpeed <= idleMaxSpeed)
    {
        targetState = &stateNames.Idle;
    }
    else if (locomotionSpeed < runMinSpeed)
    {
        targetState = &stateNames.Walk;
    }
    else
    {
        targetState = &stateNames.Run;
    }

    if (!targetState || targetState->empty() || !HasState(*targetState))
    {
        return false;
    }

    // CrossFade中に速度が変化して別Stateが必要になっても、Animatorは現在Interruptを拒否します。
    // ここで要求を捨てるとFade終了後も古いStateが再生され続けるため、最新要求だけを予約します。
    if (m_Animator.IsCrossFading())
    {
        if (m_PendingStateName == *targetState)
        {
            // 進行中の遷移先と一致しているなら追加予約は不要です。
            m_QueuedStateName.clear();
        }
        else
        {
            m_QueuedStateName = *targetState;
        }

        return true;
    }

    m_QueuedStateName.clear();
    return TransitionTo(*targetState);
}

void AnimatorStateMachine::Update(float deltaTime)
{
    const bool wasCrossFading = m_Animator.IsCrossFading();

    m_Animator.Update(deltaTime);

    if (wasCrossFading &&
        !m_Animator.IsCrossFading() &&
        !m_PendingStateName.empty())
    {
        m_CurrentStateName = std::move(m_PendingStateName);
        m_PendingStateName.clear();
    }

    // Fade中にLocomotion目標が変化していた場合、完了直後に最新Stateへ次のFadeを開始します。
    // Queueは1件だけに限定します。Locomotionでは途中経過より「現在の速度が要求する最新State」が
    // 重要なので、古い要求を順番に再生する必要がありません。
    if (!m_Animator.IsCrossFading() && !m_QueuedStateName.empty())
    {
        std::string queuedState = std::move(m_QueuedStateName);
        m_QueuedStateName.clear();

        // Fade完了時点で既にそのStateへ到達していれば追加遷移は不要です。
        if (queuedState != m_CurrentStateName)
        {
            TransitionTo(queuedState);
        }
    }
}

} // namespace Raven
