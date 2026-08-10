// Raven/Animation/AnimatorStateMachine.h
#pragma once

#include "Raven/Animation/Animator.h"
#include "Raven/Animation/AnimatorParameter.h"
#include "Raven/Animation/AnimatorTransition.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Raven
{

struct AnimatorStateDefinition
{
    std::shared_ptr<AnimationClip> Clip;
    float CrossFadeDuration = 0.2f;
    bool RestartOnEnter = true;
};

struct LocomotionStateNames
{
    std::string Idle = "Idle";
    std::string Walk = "Walk";
    std::string Run = "Run";
};

struct LocomotionThresholds
{
    float IdleMaxSpeed = 0.1f;
    float RunMinSpeed = 4.0f;
};

class AnimatorStateMachine
{
public:
    explicit AnimatorStateMachine(Animator& animator) : m_Animator(animator) {}
    bool AddState(std::string name, std::shared_ptr<AnimationClip> clip, float crossFadeDuration = 0.2f, bool restartOnEnter = true);
    bool RemoveState(const std::string& name);
    bool HasState(const std::string& name) const;
    const AnimatorStateDefinition* FindState(const std::string& name) const;
    bool SetInitialState(const std::string& name, bool startImmediately = true);
    bool TransitionTo(const std::string& name, float durationOverride = -1.0f);
    bool AddFloatParameter(std::string name, float defaultValue = 0.0f);
    bool AddBoolParameter(std::string name, bool defaultValue = false);
    bool AddTriggerParameter(std::string name);
    bool RemoveParameter(const std::string& name);
    bool HasParameter(const std::string& name) const;
    bool SetFloat(const std::string& name, float value);
    bool SetBool(const std::string& name, bool value);
    bool SetTrigger(const std::string& name);
    bool ResetTrigger(const std::string& name);
    bool GetFloat(const std::string& name, float& outValue) const;
    bool GetBool(const std::string& name, bool& outValue) const;
    bool AddTransition(AnimatorTransition transition);
    const std::vector<AnimatorTransition>& GetTransitions() const { return m_Transitions; }

    // Speed ParameterからIdle / Walk / Runを駆動する4本のTransitionを一括生成します。
    // Idle->Walk / Walk->Idle / Walk->Run / Run->Walk の境界条件を一箇所へ集約します。
    bool AddLocomotionTransitions(
        const std::string& speedParameterName = "Speed",
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& stateNames = {},
        float crossFadeDuration = 0.2f);

    // Character用の最小Locomotion Controllerを一括構築します。
    // State 3個 + Speed Float Parameter + Transition 4本 + 初期Idleを生成します。
    bool BuildLocomotionController(
        std::shared_ptr<AnimationClip> idleClip,
        std::shared_ptr<AnimationClip> walkClip,
        std::shared_ptr<AnimationClip> runClip,
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& stateNames = {},
        const std::string& speedParameterName = "Speed",
        float crossFadeDuration = 0.2f,
        bool startImmediately = true);

    bool UpdateLocomotion(float speed, const LocomotionThresholds& thresholds = {}, const LocomotionStateNames& stateNames = {});
    void Update(float deltaTime);
    bool HasCurrentState() const { return !m_CurrentStateName.empty(); }
    const std::string& GetCurrentStateName() const { return m_CurrentStateName; }
    const std::string& GetPendingStateName() const { return m_PendingStateName; }
    const std::string& GetQueuedStateName() const { return m_QueuedStateName; }
    Animator& GetAnimator() { return m_Animator; }
    const Animator& GetAnimator() const { return m_Animator; }

private:
    const AnimatorParameter* FindParameter(const std::string& name) const;
    AnimatorParameter* FindParameter(const std::string& name);
    bool EvaluateCondition(const AnimatorCondition& condition) const;
    bool EvaluateTransitions();
    void ConsumeTransitionTriggers(const AnimatorTransition& transition);

private:
    Animator& m_Animator;
    std::unordered_map<std::string, AnimatorStateDefinition> m_States;
    std::unordered_map<std::string, AnimatorParameter> m_Parameters;
    std::vector<AnimatorTransition> m_Transitions;
    std::string m_CurrentStateName;
    std::string m_PendingStateName;
    std::string m_QueuedStateName;
};

} // namespace Raven
