// Raven/Animation/AnimatorStateMachine.cpp
#include "Raven/Animation/AnimatorStateMachine.h"

#include <algorithm>
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

    // 再生中Stateを削除してもAnimatorはshared_ptrでClipを保持しているため即座に壊れません。
    // ただしState Machine上ではその名前へ戻れなくなるので、管理名は明示的に解除します。
    if (m_CurrentStateName == name)
    {
        m_CurrentStateName.clear();
    }

    if (m_PendingStateName == name)
    {
        m_PendingStateName.clear();
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

    if (startImmediately)
    {
        // 初期StateはBlend元が存在しないためCrossFadeせず直接Playします。
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

    // 既に同じStateで、遷移予約も無い場合は何もしません。
    // 毎Frame speed判定からTransitionTo("Walk")を呼ぶようなGameplayコードでも、
    // Animationを毎Frame先頭へ戻さないための重要なGuardです。
    if (m_CurrentStateName == name && m_PendingStateName.empty())
    {
        return true;
    }

    // 同じ遷移先をCrossFade中に繰り返し要求された場合も成功扱いで無視します。
    if (m_PendingStateName == name)
    {
        return true;
    }

    const float duration =
        (durationOverride >= 0.0f)
            ? durationOverride
            : target->CrossFadeDuration;

    // 初期State未設定、またはAnimator側にCurrent Clipが無い場合は直接Playします。
    if (!HasCurrentState() || !m_Animator.GetCurrentState().IsValid())
    {
        m_Animator.Play(target->Clip, target->RestartOnEnter);
        m_CurrentStateName = name;
        m_PendingStateName.clear();
        return true;
    }

    // Animator::CrossFade()がfalseを返した場合は、Fade中割り込み拒否などにより
    // 実際のAnimation状態が変わっていません。そのためState Machine側の名前も変更しません。
    if (!m_Animator.CrossFade(
            target->Clip,
            std::max(duration, 0.0f),
            target->RestartOnEnter))
    {
        return false;
    }

    // duration <= 0ではAnimator::CrossFade()内部がPlayへフォールバックし、即時遷移します。
    if (!m_Animator.IsCrossFading())
    {
        m_CurrentStateName = name;
        m_PendingStateName.clear();
        return true;
    }

    // CrossFade完了まではCurrentStateNameを旧Stateのまま維持します。
    // 「現在表示中のPose」と「遷移先」を区別できるため、Editor/Debug表示でも扱いやすくなります。
    m_PendingStateName = name;
    return true;
}

} // namespace Raven
