// Raven/Animation/AnimatorStateMachine.h
#pragma once

#include "Raven/Animation/Animator.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Raven
{

struct AnimatorStateDefinition
{
    std::shared_ptr<AnimationClip> Clip;
    float CrossFadeDuration = 0.2f;
    bool RestartOnEnter = true;
};

// ============================================================================
// LocomotionStateNames / LocomotionThresholds
// ============================================================================
// Idle / Walk / Runの名前と速度境界をGameplayコードから分離するための小さな設定です。
// まだ汎用Parameter/Conditionシステムにはせず、まず最も基本的なLocomotionを実動させます。
struct LocomotionStateNames
{
    std::string Idle = "Idle";
    std::string Walk = "Walk";
    std::string Run = "Run";
};

struct LocomotionThresholds
{
    // speed <= IdleMaxSpeed をIdleとします。
    float IdleMaxSpeed = 0.1f;

    // speed < RunMinSpeed をWalk、それ以上をRunとします。
    float RunMinSpeed = 4.0f;
};

class AnimatorStateMachine
{
public:
    explicit AnimatorStateMachine(Animator& animator)
        : m_Animator(animator)
    {
    }

    bool AddState(
        std::string name,
        std::shared_ptr<AnimationClip> clip,
        float crossFadeDuration = 0.2f,
        bool restartOnEnter = true);

    bool RemoveState(const std::string& name);
    bool HasState(const std::string& name) const;
    const AnimatorStateDefinition* FindState(const std::string& name) const;

    bool SetInitialState(const std::string& name, bool startImmediately = true);
    bool TransitionTo(const std::string& name, float durationOverride = -1.0f);

    // 移動速度からIdle / Walk / Runを選択してTransitionTo()へ変換します。
    // Gameplay側は毎Frameこの関数へ速度を渡すだけでよく、State名や閾値判定を散在させません。
    //
    // Animatorは現在Fade中の別Transition割り込みをまだ許可していないため、
    // Fade中に目標Stateが変わった場合はm_QueuedStateNameへ保存し、Fade完了直後に適用します。
    bool UpdateLocomotion(
        float speed,
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& stateNames = {});

    void Update(float deltaTime);

    bool HasCurrentState() const { return !m_CurrentStateName.empty(); }
    const std::string& GetCurrentStateName() const { return m_CurrentStateName; }
    const std::string& GetPendingStateName() const { return m_PendingStateName; }

    // Fade中に次の目標Stateが変化した場合の予約先です。
    // 例: Idle -> WalkのFade中に速度がRun領域へ入った場合、Runをここへ保持します。
    const std::string& GetQueuedStateName() const { return m_QueuedStateName; }

    Animator& GetAnimator() { return m_Animator; }
    const Animator& GetAnimator() const { return m_Animator; }

private:
    Animator& m_Animator;
    std::unordered_map<std::string, AnimatorStateDefinition> m_States;

    std::string m_CurrentStateName;
    std::string m_PendingStateName;
    std::string m_QueuedStateName;
};

} // namespace Raven
