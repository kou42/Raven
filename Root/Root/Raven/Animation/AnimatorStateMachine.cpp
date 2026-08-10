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

    // State名はState Machine内で一意にします。
    // 同名Stateを上書き可能にすると、再生中Clipと定義テーブルの対応が暗黙に変化するため、
    // 登録ミスを早い段階で検出できるよう明示的に失敗させます。
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

    if (m_QueuedStateName == name)
    {
        m_QueuedStateName.clear();
    }

    // このStateを参照するTransitionも無効になるため同時に除去します。
    m_Transitions.erase(
        std::remove_if(
            m_Transitions.begin(),
            m_Transitions.end(),
            [&name](const AnimatorTransition& transition)
            {
                return transition.FromState == name || transition.ToState == name;
            }),
        m_Transitions.end());

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
    // Gameplay側は「現在Fade中か」を意識せず、必要なStateを毎Frame要求できます。
    if (m_PendingStateName == name)
    {
        return true;
    }

    const float duration =
        (durationOverride >= 0.0f)
            ? durationOverride
            : target->CrossFadeDuration;

    // 初期State未設定、またはAnimator側にCurrent Clipが無い場合は直接Playします。
    // Blend元が存在しない状態でCrossFadeを作る意味がないためです。
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
    // その場合Pendingを経由せず、この場でCurrent State名を確定します。
    if (!m_Animator.IsCrossFading())
    {
        m_CurrentStateName = name;
        m_PendingStateName.clear();
        return true;
    }

    // CrossFade完了まではCurrentStateNameを旧Stateのまま維持します。
    // 「現在の遷移元」と「遷移先」をCurrent/Pendingとして区別できるため、
    // Editor/Debug表示や将来のTransition Conditionでも状態を解釈しやすくなります。
    m_PendingStateName = name;
    return true;
}

bool AnimatorStateMachine::AddFloatParameter(std::string name, float defaultValue)
{
    if (name.empty() || !std::isfinite(defaultValue) || HasParameter(name))
    {
        return false;
    }

    AnimatorParameter parameter{};
    parameter.Name = name;
    parameter.Type = AnimatorParameterType::Float;
    parameter.Value = defaultValue;

    m_Parameters.emplace(std::move(name), std::move(parameter));
    return true;
}

bool AnimatorStateMachine::AddBoolParameter(std::string name, bool defaultValue)
{
    if (name.empty() || HasParameter(name))
    {
        return false;
    }

    AnimatorParameter parameter{};
    parameter.Name = name;
    parameter.Type = AnimatorParameterType::Bool;
    parameter.Value = defaultValue;

    m_Parameters.emplace(std::move(name), std::move(parameter));
    return true;
}

bool AnimatorStateMachine::AddTriggerParameter(std::string name)
{
    if (name.empty() || HasParameter(name))
    {
        return false;
    }

    AnimatorParameter parameter{};
    parameter.Name = name;
    parameter.Type = AnimatorParameterType::Trigger;
    parameter.Value = false;

    m_Parameters.emplace(std::move(name), std::move(parameter));
    return true;
}

bool AnimatorStateMachine::RemoveParameter(const std::string& name)
{
    const auto it = m_Parameters.find(name);
    if (it == m_Parameters.end())
    {
        return false;
    }

    // Conditionが参照するParameterを削除するとTransition定義が壊れるため、
    // そのParameterを使うTransitionも同時に除去します。
    m_Transitions.erase(
        std::remove_if(
            m_Transitions.begin(),
            m_Transitions.end(),
            [&name](const AnimatorTransition& transition)
            {
                return std::any_of(
                    transition.Conditions.begin(),
                    transition.Conditions.end(),
                    [&name](const AnimatorCondition& condition)
                    {
                        return condition.ParameterName == name;
                    });
            }),
        m_Transitions.end());

    m_Parameters.erase(it);
    return true;
}

bool AnimatorStateMachine::HasParameter(const std::string& name) const
{
    return m_Parameters.find(name) != m_Parameters.end();
}

const AnimatorParameter* AnimatorStateMachine::FindParameter(const std::string& name) const
{
    const auto it = m_Parameters.find(name);
    return (it != m_Parameters.end()) ? &it->second : nullptr;
}

AnimatorParameter* AnimatorStateMachine::FindParameter(const std::string& name)
{
    const auto it = m_Parameters.find(name);
    return (it != m_Parameters.end()) ? &it->second : nullptr;
}

bool AnimatorStateMachine::SetFloat(const std::string& name, float value)
{
    AnimatorParameter* parameter = FindParameter(name);
    if (!parameter ||
        parameter->Type != AnimatorParameterType::Float ||
        !std::isfinite(value))
    {
        return false;
    }

    parameter->Value = value;
    return true;
}

bool AnimatorStateMachine::SetBool(const std::string& name, bool value)
{
    AnimatorParameter* parameter = FindParameter(name);
    if (!parameter || parameter->Type != AnimatorParameterType::Bool)
    {
        return false;
    }

    parameter->Value = value;
    return true;
}

bool AnimatorStateMachine::SetTrigger(const std::string& name)
{
    AnimatorParameter* parameter = FindParameter(name);
    if (!parameter || parameter->Type != AnimatorParameterType::Trigger)
    {
        return false;
    }

    parameter->Value = true;
    return true;
}

bool AnimatorStateMachine::ResetTrigger(const std::string& name)
{
    AnimatorParameter* parameter = FindParameter(name);
    if (!parameter || parameter->Type != AnimatorParameterType::Trigger)
    {
        return false;
    }

    parameter->Value = false;
    return true;
}

bool AnimatorStateMachine::GetFloat(const std::string& name, float& outValue) const
{
    const AnimatorParameter* parameter = FindParameter(name);
    if (!parameter || parameter->Type != AnimatorParameterType::Float)
    {
        return false;
    }

    const float* value = std::get_if<float>(&parameter->Value);
    if (!value)
    {
        return false;
    }

    outValue = *value;
    return true;
}

bool AnimatorStateMachine::GetBool(const std::string& name, bool& outValue) const
{
    const AnimatorParameter* parameter = FindParameter(name);
    if (!parameter ||
        (parameter->Type != AnimatorParameterType::Bool &&
         parameter->Type != AnimatorParameterType::Trigger))
    {
        return false;
    }

    const bool* value = std::get_if<bool>(&parameter->Value);
    if (!value)
    {
        return false;
    }

    outValue = *value;
    return true;
}

bool AnimatorStateMachine::AddTransition(AnimatorTransition transition)
{
    if (!HasState(transition.FromState) ||
        !HasState(transition.ToState) ||
        transition.FromState == transition.ToState)
    {
        return false;
    }

    transition.CrossFadeDuration = std::max(transition.CrossFadeDuration, 0.0f);

    for (const AnimatorCondition& condition : transition.Conditions)
    {
        const AnimatorParameter* parameter = FindParameter(condition.ParameterName);
        if (!parameter)
        {
            return false;
        }

        // ConditionのExpectedValue型がParameter型と一致していることを登録時に検証します。
        // Runtime評価中にvariant型違いを毎回エラー扱いするより、定義ミスを早期検出します。
        if (parameter->Type == AnimatorParameterType::Float)
        {
            if (!std::holds_alternative<float>(condition.ExpectedValue))
            {
                return false;
            }
        }
        else
        {
            if (!std::holds_alternative<bool>(condition.ExpectedValue))
            {
                return false;
            }

            // Bool/Triggerへ数値比較演算を使う定義は意味が曖昧なので拒否します。
            if (condition.Operator != AnimatorConditionOperator::Equal &&
                condition.Operator != AnimatorConditionOperator::NotEqual)
            {
                return false;
            }
        }
    }

    m_Transitions.push_back(std::move(transition));
    return true;
}

bool AnimatorStateMachine::EvaluateCondition(const AnimatorCondition& condition) const
{
    const AnimatorParameter* parameter = FindParameter(condition.ParameterName);
    if (!parameter)
    {
        return false;
    }

    if (parameter->Type == AnimatorParameterType::Float)
    {
        const float* actual = std::get_if<float>(&parameter->Value);
        const float* expected = std::get_if<float>(&condition.ExpectedValue);
        if (!actual || !expected)
        {
            return false;
        }

        switch (condition.Operator)
        {
        case AnimatorConditionOperator::Equal:        return *actual == *expected;
        case AnimatorConditionOperator::NotEqual:     return *actual != *expected;
        case AnimatorConditionOperator::Greater:      return *actual > *expected;
        case AnimatorConditionOperator::GreaterEqual: return *actual >= *expected;
        case AnimatorConditionOperator::Less:         return *actual < *expected;
        case AnimatorConditionOperator::LessEqual:    return *actual <= *expected;
        }

        return false;
    }

    const bool* actual = std::get_if<bool>(&parameter->Value);
    const bool* expected = std::get_if<bool>(&condition.ExpectedValue);
    if (!actual || !expected)
    {
        return false;
    }

    switch (condition.Operator)
    {
    case AnimatorConditionOperator::Equal:    return *actual == *expected;
    case AnimatorConditionOperator::NotEqual: return *actual != *expected;
    default:                                  return false;
    }
}

bool AnimatorStateMachine::EvaluateTransitions()
{
    // Current Stateが無い、またはFade中の場合は自動Transitionを開始しません。
    // Fade割り込み対応はAnimator側のSnapshot Pose実装後にポリシーを拡張します。
    if (!HasCurrentState() || m_Animator.IsCrossFading())
    {
        return false;
    }

    for (const AnimatorTransition& transition : m_Transitions)
    {
        if (transition.FromState != m_CurrentStateName)
        {
            continue;
        }

        // Conditionを持たないTransitionは常時成立になります。
        // 将来Exit Timeを追加する場合にも使えるため許可しています。
        const bool allConditionsMet = std::all_of(
            transition.Conditions.begin(),
            transition.Conditions.end(),
            [this](const AnimatorCondition& condition)
            {
                return EvaluateCondition(condition);
            });

        if (!allConditionsMet)
        {
            continue;
        }

        // 登録順を優先順位として、最初に成立したTransitionだけを実行します。
        // Transition開始に失敗した場合はTriggerを消費せず、次Frameに再評価できます。
        if (!TransitionTo(transition.ToState, transition.CrossFadeDuration))
        {
            return false;
        }

        ConsumeTransitionTriggers(transition);
        return true;
    }

    return false;
}

void AnimatorStateMachine::ConsumeTransitionTriggers(const AnimatorTransition& transition)
{
    for (const AnimatorCondition& condition : transition.Conditions)
    {
        AnimatorParameter* parameter = FindParameter(condition.ParameterName);
        if (!parameter || parameter->Type != AnimatorParameterType::Trigger)
        {
            continue;
        }

        // Triggerは「成立したTransitionが実際に開始した」時点でのみ消費します。
        // CrossFade割り込み拒否などで遷移できなかった場合はSetTrigger状態を維持します。
        parameter->Value = false;
    }
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

    // 設定値が負、またはRun境界がIdle境界より小さくても判定範囲が反転しないよう
    // Runtime側で安全な順序へ正規化します。
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

    // Locomotion設定で指定されたStateが未登録なら、名前を暗黙に作らず失敗させます。
    // Asset/Controller設定ミスをAnimation停止ではなく戻り値で検出できるようにします。
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
            // 以前予約した別Stateがあった場合も、最新入力がPendingへ戻ったので取り消します。
            m_QueuedStateName.clear();
        }
        else
        {
            // Locomotionでは入力履歴をすべて再生する必要はありません。
            // 最新速度が要求するStateだけを1件保持し、古い予約は上書きします。
            m_QueuedStateName = *targetState;
        }

        return true;
    }

    // Fade外ならQueueは不要なので直接Transition要求へ変換します。
    m_QueuedStateName.clear();
    return TransitionTo(*targetState);
}

void AnimatorStateMachine::Update(float deltaTime)
{
    // Update開始時にParameter Transitionを評価します。
    // Gameplay側でSetFloat/SetBool/SetTriggerした値が同FrameのAnimation遷移へ反映されます。
    // Locomotion Queueがある場合は明示的な最新要求を優先し、自動Transition評価は次Frameへ回します。
    if (!m_Animator.IsCrossFading() && m_QueuedStateName.empty())
    {
        EvaluateTransitions();
    }

    // Update前後のCrossFade状態を比較することで、Animator内部のFade完了を検出します。
    // State Machine側にFade時間を重複保持しないことが重要です。
    const bool wasCrossFading = m_Animator.IsCrossFading();

    m_Animator.Update(deltaTime);

    // Pending Stateが存在する状態でAnimatorのCrossFadeが完了した瞬間に、
    // State Machine側のCurrent名を遷移先へ確定します。
    // AnimatorStateの昇格処理と同じFrameで同期することで、Animation実体と名前がずれません。
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
