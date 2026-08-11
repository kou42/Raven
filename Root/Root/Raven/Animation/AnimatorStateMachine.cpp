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
    // 同名Stateを上書き可能にすると、再生中Motionと定義テーブルの対応が暗黙に変化するため、
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

bool AnimatorStateMachine::AddBlendTreeState(
    std::string name,
    std::shared_ptr<BlendTree1D> blendTree,
    std::string blendParameterName,
    float crossFadeDuration,
    bool restartOnEnter)
{
    if (name.empty() || !blendTree || blendTree->GetChildCount() == 0 ||
        blendParameterName.empty() || HasState(name))
    {
        return false;
    }

    // Blend Treeが参照するParameterはFloatに限定します。
    // Bool/Triggerを暗黙に0/1へ変換するとEditor定義ミスを見逃しやすいため登録時に拒否します。
    float unusedValue = 0.0f;
    if (!GetFloat(blendParameterName, unusedValue))
    {
        return false;
    }

    AnimatorStateDefinition state{};
    state.BlendTree = std::move(blendTree);
    state.BlendParameterName = std::move(blendParameterName);
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

    // 再生中Stateを削除してもAnimatorはshared_ptrでMotionを保持しているため即座に壊れません。
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

bool AnimatorStateMachine::GetBlendParameterValue(
    const AnimatorStateDefinition& state,
    float& outValue) const
{
    if (!state.IsBlendTreeState() || state.BlendParameterName.empty())
    {
        return false;
    }

    return GetFloat(state.BlendParameterName, outValue);
}

bool AnimatorStateMachine::SetInitialState(
    const std::string& name,
    bool startImmediately)
{
    const AnimatorStateDefinition* state = FindState(name);
    if (!state || !state->IsValid())
    {
        return false;
    }

    m_CurrentStateName = name;
    m_PendingStateName.clear();
    m_QueuedStateName.clear();

    if (startImmediately)
    {
        // 初期StateはBlend元が存在しないためCrossFadeせず直接Playします。
        if (state->IsBlendTreeState())
        {
            float blendParameter = 0.0f;
            if (!GetBlendParameterValue(*state, blendParameter))
            {
                return false;
            }

            m_Animator.PlayBlendTree(state->BlendTree, blendParameter, true);
        }
        else
        {
            m_Animator.Play(state->Clip, true);
        }
    }

    return true;
}

bool AnimatorStateMachine::TransitionTo(
    const std::string& name,
    float durationOverride)
{
    const AnimatorStateDefinition* target = FindState(name);
    if (!target || !target->IsValid())
    {
        return false;
    }

    // 既に同じStateで、遷移予約も無い場合は何もしません。
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

    float targetBlendParameter = 0.0f;
    if (target->IsBlendTreeState() &&
        !GetBlendParameterValue(*target, targetBlendParameter))
    {
        return false;
    }

    // 初期State未設定、またはAnimator側にCurrent Motionが無い場合は直接Playします。
    if (!HasCurrentState() || !m_Animator.GetCurrentState().IsValid())
    {
        if (target->IsBlendTreeState())
        {
            m_Animator.PlayBlendTree(
                target->BlendTree,
                targetBlendParameter,
                target->RestartOnEnter);
        }
        else
        {
            m_Animator.Play(target->Clip, target->RestartOnEnter);
        }

        m_CurrentStateName = name;
        m_PendingStateName.clear();
        return true;
    }

    // Clip / BlendTreeの種類に応じてAnimatorの対応CrossFadeへ変換します。
    const bool transitionStarted = target->IsBlendTreeState()
        ? m_Animator.CrossFadeBlendTree(
            target->BlendTree,
            targetBlendParameter,
            std::max(duration, 0.0f),
            target->RestartOnEnter)
        : m_Animator.CrossFade(
            target->Clip,
            std::max(duration, 0.0f),
            target->RestartOnEnter);

    // AnimatorがFade中割り込みを拒否した場合など、実際のMotion状態が変わっていないため
    // State Machine側の名前も変更しません。
    if (!transitionStarted)
    {
        return false;
    }

    // duration <= 0ではAnimator側がPlayへフォールバックし、即時遷移します。
    if (!m_Animator.IsCrossFading())
    {
        m_CurrentStateName = name;
        m_PendingStateName.clear();
        return true;
    }

    // CrossFade完了まではCurrentStateNameを旧Stateのまま維持します。
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

    // Blend Tree Stateが直接参照するParameterを削除するとMotion評価自体が不可能になるため、
    // Transition Conditionとは異なり自動削除せず、先にState定義を直すことを要求します。
    for (const auto& statePair : m_States)
    {
        if (statePair.second.IsBlendTreeState() &&
            statePair.second.BlendParameterName == name)
        {
            return false;
        }
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

    // Exit TimeはNormalized Timeとして扱うため、0.0～1.0の範囲だけを許可します。
    if (transition.HasExitTime &&
        (!std::isfinite(transition.ExitTime) ||
         transition.ExitTime < 0.0f ||
         transition.ExitTime > 1.0f))
    {
        return false;
    }

    for (const AnimatorCondition& condition : transition.Conditions)
    {
        const AnimatorParameter* parameter = FindParameter(condition.ParameterName);
        if (!parameter)
        {
            return false;
        }

        // ConditionのExpectedValue型がParameter型と一致していることを登録時に検証します。
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

void AnimatorStateMachine::SyncAnimatorBlendTreeParameters()
{
    // Current StateがBlend Treeなら、同FrameにGameplay側がSetFloatした値をPose評価前に反映します。
    if (!m_CurrentStateName.empty())
    {
        const AnimatorStateDefinition* current = FindState(m_CurrentStateName);
        if (current && current->IsBlendTreeState())
        {
            float value = 0.0f;
            if (GetBlendParameterValue(*current, value))
            {
                m_Animator.SetCurrentBlendParameter(value);
            }
        }
    }

    // CrossFade先がBlend Treeの場合はNext Stateにも同じFrameのParameter値を反映します。
    if (!m_PendingStateName.empty())
    {
        const AnimatorStateDefinition* pending = FindState(m_PendingStateName);
        if (pending && pending->IsBlendTreeState())
        {
            float value = 0.0f;
            if (GetBlendParameterValue(*pending, value))
            {
                m_Animator.SetNextBlendParameter(value);
            }
        }
    }
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
    // Priorityは「同じFrameに複数Transitionが成立した場合」の選択順だけを決めます。
    if (!HasCurrentState() || m_Animator.IsCrossFading())
    {
        return false;
    }

    const AnimatorTransition* selectedTransition = nullptr;

    for (const AnimatorTransition& transition : m_Transitions)
    {
        if (transition.FromState != m_CurrentStateName)
        {
            continue;
        }

        if (transition.HasExitTime &&
            m_Animator.GetNormalizedTime() < transition.ExitTime)
        {
            continue;
        }

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

        // より高いPriorityのTransitionが見つかった場合だけ候補を置き換えます。
        // 同Priorityでは置き換えないため、vectorへ登録された順番がTie Breakとして残ります。
        if (!selectedTransition || transition.Priority > selectedTransition->Priority)
        {
            selectedTransition = &transition;
        }
    }

    if (!selectedTransition)
    {
        return false;
    }

    if (!TransitionTo(selectedTransition->ToState, selectedTransition->CrossFadeDuration))
    {
        return false;
    }

    ConsumeTransitionTriggers(*selectedTransition);
    return true;
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
        parameter->Value = false;
    }
}

bool AnimatorStateMachine::UpdateLocomotion(
    float speed,
    const LocomotionThresholds& thresholds,
    const LocomotionStateNames& stateNames)
{
    // Velocityの向きではなく移動量だけを使うため負値も絶対値へ正規化します。
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

    if (m_Animator.IsCrossFading())
    {
        if (m_PendingStateName == *targetState)
        {
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
    // Blend Tree ParameterはTransition評価より前に同期します。
    // これにより同FrameのSpeed変更がLocomotion PoseとTransition Conditionの両方へ一致して反映されます。
    SyncAnimatorBlendTreeParameters();

    if (!m_Animator.IsCrossFading() && m_QueuedStateName.empty())
    {
        EvaluateTransitions();
    }

    // EvaluateTransitions()でBlendTree Stateへ遷移した可能性があるため、Next側もここでもう一度同期します。
    SyncAnimatorBlendTreeParameters();

    const bool wasCrossFading = m_Animator.IsCrossFading();

    m_Animator.Update(deltaTime);

    if (wasCrossFading &&
        !m_Animator.IsCrossFading() &&
        !m_PendingStateName.empty())
    {
        m_CurrentStateName = std::move(m_PendingStateName);
        m_PendingStateName.clear();
    }

    if (!m_Animator.IsCrossFading() && !m_QueuedStateName.empty())
    {
        std::string queuedState = std::move(m_QueuedStateName);
        m_QueuedStateName.clear();

        if (queuedState != m_CurrentStateName)
        {
            TransitionTo(queuedState);
        }
    }
}

} // namespace Raven
