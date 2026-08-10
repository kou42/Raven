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

// ============================================================================
// AnimatorStateDefinition
// ============================================================================
// State Machine側で保持する「名前付きAnimation Stateの定義」です。
// AnimatorStateがTimeなどの実行時状態を持つのに対し、こちらはClipと遷移設定だけを持ちます。
//
// まずはIdle / Walk / Runを安全に切り替えるための薄い層に限定し、Parameter / Condition /
// BlendTreeなどはここへ詰め込まず、必要になった段階で別構造として拡張します。
struct AnimatorStateDefinition
{
    std::shared_ptr<AnimationClip> Clip;

    // このStateへ遷移する際の標準CrossFade時間[秒]です。
    float CrossFadeDuration = 0.2f;

    // Stateへ入り直した際にClip先頭から再生するかを指定します。
    bool RestartOnEnter = true;
};

// ============================================================================
// LocomotionStateNames / LocomotionThresholds
// ============================================================================
// Idle / Walk / Runの名前と速度境界をGameplayコードから分離するための小さな設定です。
// 汎用Parameter/Transitionを追加した後も、最小Locomotionをすぐ使える便利APIとして維持します。
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

// ============================================================================
// CharacterAnimationParameters
// ============================================================================
// Character ControllerとAnimation State Machineの間で共有するParameter名をまとめます。
// 文字列をGameplay側へ散在させず、将来Crouch / Falling等を追加する際にも同じ窓口を拡張できます。
struct CharacterAnimationParameters
{
    std::string Speed = "Speed";
    std::string Grounded = "Grounded";
    std::string Jump = "Jump";
};

// Jump State名をLocomotion State名とは分離して保持します。
// 将来JumpStart / Fall / Landへ細分化してもLocomotion側の設定を変更せず拡張できます。
struct JumpStateNames
{
    std::string Jump = "Jump";
};

// ============================================================================
// AnimatorStateMachine
// ============================================================================
// Animatorの上に置く薄いState管理層です。
//
// 責務は次の3つを中心に限定します。
//  1. "Idle" / "Walk" / "Run" のような名前とAnimationClipを対応付ける
//  2. Current / Pending / QueuedのState名を管理する
//  3. Parameter / ConditionからState変更要求を作り、Animator::Play / CrossFadeへ変換する
//
// Animation時間、Loop、Pose Sample、CrossFade WeightなどのRuntime処理はAnimator側へ残します。
// これによりState MachineがAnimation再生処理を二重管理しない構造になります。
//
// Queued StateはAnimatorがCrossFade割り込みに未対応な現在の段階で、Fade中に発生した
// 「最新の遷移要求」を失わないための薄い補助機構です。将来Snapshot Poseを使ったInterrupt
// TransitionをAnimatorへ実装した場合、このQueueポリシーは置き換え可能です。
class AnimatorStateMachine
{
public:
    explicit AnimatorStateMachine(Animator& animator)
        : m_Animator(animator)
    {
    }

    // 名前付きStateを登録します。
    // 同名Stateが既に存在する場合はfalseを返し、意図しない上書きを防ぎます。
    bool AddState(std::string name, std::shared_ptr<AnimationClip> clip, float crossFadeDuration = 0.2f, bool restartOnEnter = true);
    bool RemoveState(const std::string& name);
    bool HasState(const std::string& name) const;
    const AnimatorStateDefinition* FindState(const std::string& name) const;
    bool SetInitialState(const std::string& name, bool startImmediately = true);
    bool TransitionTo(const std::string& name, float durationOverride = -1.0f);

    // ------------------------------------------------------------------------
    // Animator Parameter
    // ------------------------------------------------------------------------
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

    // ------------------------------------------------------------------------
    // Animator Transition
    // ------------------------------------------------------------------------
    bool AddTransition(AnimatorTransition transition);
    const std::vector<AnimatorTransition>& GetTransitions() const { return m_Transitions; }

    // Speed Parameterを使うIdle / Walk / Run用の4本のTransitionを一括登録します。
    bool AddLocomotionTransitions(const std::string& speedParameterName = "Speed", const LocomotionThresholds& thresholds = {}, const LocomotionStateNames& stateNames = {}, float crossFadeDuration = 0.2f);

    // Idle / Walk / Run State、Speed Parameter、4本のTransition、初期Idleをまとめて構築します。
    bool BuildLocomotionController(std::shared_ptr<AnimationClip> idleClip, std::shared_ptr<AnimationClip> walkClip, std::shared_ptr<AnimationClip> runClip, const LocomotionThresholds& thresholds = {}, const LocomotionStateNames& stateNames = {}, const std::string& speedParameterName = "Speed", float crossFadeDuration = 0.2f, bool startImmediately = true);

    bool AddCharacterParameters(const CharacterAnimationParameters& parameterNames = {}, bool initialGrounded = true);
    bool UpdateCharacterParameters(float speed, bool grounded, bool jumpRequested, const CharacterAnimationParameters& parameterNames = {});

    // Jump StateとLocomotion <-> JumpのTransitionを追加します。
    // Any Stateはまだ導入していないため、Idle / Walk / RunそれぞれからJumpへの遷移を明示的に作ります。
    // Jump開始条件は Jump Trigger == true AND Grounded == true とし、空中での再Jumpを防ぎます。
    // 着地後はSpeed境界を使ってJumpからIdle / Walk / Runへ直接戻します。
    bool AddJumpStateAndTransitions(
        std::shared_ptr<AnimationClip> jumpClip,
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& locomotionStateNames = {},
        const JumpStateNames& jumpStateNames = {},
        const CharacterAnimationParameters& parameterNames = {},
        float crossFadeDuration = 0.15f);

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
