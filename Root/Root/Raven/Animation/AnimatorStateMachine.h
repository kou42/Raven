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
// AnimatorStateがTimeなどの実行時状態を持つのに対し、こちらはMotionと遷移設定だけを持ちます。
//
// Motionは従来のAnimationClip、または1D Blend Treeのどちらか一方を持ちます。
// Blend TreeのParameter実値はEntityごとに異なるRuntime値なので、この定義には名前だけを保持し、
// Animatorへ再生開始/更新する際にState MachineのParameter値を同期します。
struct AnimatorStateDefinition
{
    std::shared_ptr<AnimationClip> Clip;
    std::shared_ptr<BlendTree1D> BlendTree;
    std::string BlendParameterName;

    // このStateへ遷移する際の標準CrossFade時間[秒]です。
    float CrossFadeDuration = 0.2f;

    // Stateへ入り直した際にMotion先頭から再生するかを指定します。
    bool RestartOnEnter = true;

    bool IsBlendTreeState() const
    {
        return BlendTree != nullptr;
    }

    bool IsValid() const
    {
        return Clip != nullptr || BlendTree != nullptr;
    }
};

// ============================================================================
// LocomotionStateNames / LocomotionThresholds
// ============================================================================
// Idle / Walk / Runの名前と速度境界をGameplayコードから分離するための小さな設定です。
// 従来の3-State Locomotionを維持しつつ、下記LocomotionBlendTreeSettingsで
// 1-State + Blend Tree構成も選択できます。
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

// 1D Blend Tree版LocomotionのState名とThresholdです。
// Idle / Walk / Runを別Stateへ分けず、Speed 1つで3 Motionを連続補間します。
struct LocomotionBlendTreeSettings
{
    std::string StateName = "Locomotion";
    float IdleThreshold = 0.0f;
    float WalkThreshold = 2.0f;
    float RunThreshold = 6.0f;
};

// Character ControllerとAnimation State Machine間で共有するParameter名です。
// 文字列をGameplayコードへ散在させず、将来Parameterを追加する際の変更箇所を集約します。
struct CharacterAnimationParameters
{
    std::string Speed = "Speed";
    std::string Grounded = "Grounded";
    std::string Jump = "Jump";

    // JumpStart -> Fallの判定に使う上下方向速度です。
    // Animation側で「そろそろ落下したはず」と時間推測せず、Physics / Character Controllerが
    // 持っている実際の運動状態をParameterとして受け取ることで両者を同期します。
    std::string VerticalVelocity = "VerticalVelocity";
};

// Jump系State名はLocomotionと分離します。
// JumpStart / Fall / Landへ分割してもLocomotion設定をそのまま維持できます。
struct JumpStateNames
{
    std::string JumpStart = "JumpStart";
    std::string Fall = "Fall";
    std::string Land = "Land";
};

// ============================================================================
// AnimatorStateMachine
// ============================================================================
// Animatorの上に置く薄いState管理層です。
//
// 責務は次の3つを中心に限定します。
//  1. State名とAnimation Motion(Clip / BlendTree)を対応付ける
//  2. Current / Pending / QueuedのState名を管理する
//  3. Parameter / ConditionからState変更要求を作り、Animator::Play / CrossFadeへ変換する
//
// Animation時間、Loop、Pose Sample、CrossFade WeightなどのRuntime処理はAnimator側へ残します。
// Blend TreeのParameter値だけはState Machineが所有するParameterからAnimatorへ同期します。
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

    // 名前付きClip Stateを登録します。
    // 同名Stateが既に存在する場合はfalseを返し、意図しない上書きを防ぎます。
    bool AddState(
        std::string name,
        std::shared_ptr<AnimationClip> clip,
        float crossFadeDuration = 0.2f,
        bool restartOnEnter = true);

    // 名前付き1D Blend Tree Stateを登録します。
    // blendParameterNameは登録済みFloat Parameterである必要があります。
    bool AddBlendTreeState(
        std::string name,
        std::shared_ptr<BlendTree1D> blendTree,
        std::string blendParameterName,
        float crossFadeDuration = 0.2f,
        bool restartOnEnter = true);

    // 登録済みStateを削除します。
    // Current / Pending / Queuedに該当する名前だった場合は対応する管理名も解除します。
    bool RemoveState(const std::string& name);
    bool HasState(const std::string& name) const;

    // State定義への参照を取得します。未登録の場合はnullptrです。
    const AnimatorStateDefinition* FindState(const std::string& name) const;

    // 初期Stateを設定します。
    // startImmediately=trueならAnimatorの対応Motion再生まで行い、そのStateを現在Stateにします。
    bool SetInitialState(const std::string& name, bool startImmediately = true);

    // 登録済みStateへ遷移します。
    // durationOverride < 0 の場合はState定義のCrossFadeDurationを使用します。
    // Clip / BlendTreeの種類に応じてAnimatorの対応CrossFade APIへ変換します。
    bool TransitionTo(const std::string& name, float durationOverride = -1.0f);

    // ------------------------------------------------------------------------
    // Animator Parameter
    // ------------------------------------------------------------------------
    // Parameter名もState名と同様に一意です。
    // 型違いのSet/Getを誤って行った場合はfalseを返し、暗黙変換は行いません。
    bool AddFloatParameter(std::string name, float defaultValue = 0.0f);
    bool AddBoolParameter(std::string name, bool defaultValue = false);
    bool AddTriggerParameter(std::string name);
    bool RemoveParameter(const std::string& name);
    bool HasParameter(const std::string& name) const;

    bool SetFloat(const std::string& name, float value);
    bool SetBool(const std::string& name, bool value);

    // TriggerはGameplay/Event側からSetTrigger()され、成立したTransitionが実際に開始した時だけ
    // 自動的にfalseへ戻します。Fade中でTransition開始に失敗した場合は消費しません。
    bool SetTrigger(const std::string& name);
    bool ResetTrigger(const std::string& name);

    bool GetFloat(const std::string& name, float& outValue) const;
    bool GetBool(const std::string& name, bool& outValue) const;

    // ------------------------------------------------------------------------
    // Animator Transition
    // ------------------------------------------------------------------------
    // From/To Stateが登録済みで、Conditionが参照するParameterも存在する場合だけ登録します。
    // ConditionsはAND評価です。同時成立時はPriorityが高いものを優先し、同値なら登録順を維持します。
    bool AddTransition(AnimatorTransition transition);

    // Any Stateは「現在Stateに関係なく同じ条件で同じStateへ遷移したい」場合の便利APIです。
    // 現段階では専用Runtime Transition型を増やさず、登録時点で存在する全StateからTo Stateへの
    // 通常Transitionへ展開します。これにより既存のTransition実行経路をそのまま再利用できます。
    //
    // priorityは通常Transitionより高い値を指定することで、同FrameにLocomotionとJumpなどが
    // 同時成立してもAny State側を先に選択できます。既定値100は通常Transitionの既定値0より高くします。
    //
    // 注意: 登録後にStateを追加しても自動では対象にならないため、Controller Builderでは
    // 全Stateを登録し終えた後にAny State Transitionを追加します。将来Editorから動的にState定義を
    // 編集する段階では、専用Any State表現へ昇格させる余地を残しています。
    bool AddAnyStateTransition(
        const std::string& toState,
        std::vector<AnimatorCondition> conditions,
        float crossFadeDuration = 0.2f,
        int priority = 100)
    {
        if (toState.empty() || !HasState(toState))
        {
            return false;
        }

        for (const auto& statePair : m_States)
        {
            const std::string& fromState = statePair.first;
            if (fromState == toState)
            {
                continue;
            }

            AnimatorTransition transition{};
            transition.FromState = fromState;
            transition.ToState = toState;
            transition.CrossFadeDuration = crossFadeDuration;
            transition.Priority = priority;
            transition.Conditions = conditions;

            if (!AddTransition(std::move(transition)))
            {
                return false;
            }
        }

        return true;
    }

    const std::vector<AnimatorTransition>& GetTransitions() const
    {
        return m_Transitions;
    }

    // Speed Parameterを使うIdle / Walk / Run用の4本のTransitionを一括登録します。
    bool AddLocomotionTransitions(
        const std::string& speedParameterName = "Speed",
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& stateNames = {},
        float crossFadeDuration = 0.2f);

    // Idle / Walk / Run State、Speed Parameter、4本のTransition、初期Idleをまとめて構築します。
    // 従来3-State方式の比較・学習用として維持します。
    bool BuildLocomotionController(
        std::shared_ptr<AnimationClip> idleClip,
        std::shared_ptr<AnimationClip> walkClip,
        std::shared_ptr<AnimationClip> runClip,
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& stateNames = {},
        const std::string& speedParameterName = "Speed",
        float crossFadeDuration = 0.2f,
        bool startImmediately = true);

    // Grounded Bool / Jump Trigger / VerticalVelocity Floatを追加します。
    // Speedが既にLocomotion側で登録済みでも使用できます。
    bool AddCharacterParameters(
        const CharacterAnimationParameters& parameterNames = {},
        bool initialGrounded = true);

    // 物理・入力側からSpeed / Grounded / Jump要求 / 上下方向速度をまとめて同期します。
    bool UpdateCharacterParameters(
        float speed,
        bool grounded,
        bool jumpRequested,
        float verticalVelocity,
        const CharacterAnimationParameters& parameterNames = {});

    // JumpStart / Fall / Land Stateを追加して従来3-State Locomotionとの空中遷移を構築します。
    bool AddJumpStatesAndTransitions(
        std::shared_ptr<AnimationClip> jumpStartClip,
        std::shared_ptr<AnimationClip> fallClip,
        std::shared_ptr<AnimationClip> landClip,
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& locomotionStateNames = {},
        const JumpStateNames& jumpStateNames = {},
        const CharacterAnimationParameters& parameterNames = {},
        float crossFadeDuration = 0.15f);

    // 従来のIdle / Walk / Run / JumpStart / Fall / Land Controllerです。
    bool BuildCharacterController(
        std::shared_ptr<AnimationClip> idleClip,
        std::shared_ptr<AnimationClip> walkClip,
        std::shared_ptr<AnimationClip> runClip,
        std::shared_ptr<AnimationClip> jumpStartClip,
        std::shared_ptr<AnimationClip> fallClip,
        std::shared_ptr<AnimationClip> landClip,
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& locomotionStateNames = {},
        const JumpStateNames& jumpStateNames = {},
        const CharacterAnimationParameters& parameterNames = {},
        float locomotionCrossFadeDuration = 0.2f,
        float jumpCrossFadeDuration = 0.15f,
        bool initialGrounded = true,
        bool startImmediately = true);

    // Locomotionを1つのBlendTree StateへまとめたCharacter Controllerを構築します。
    // Speed ParameterはIdle / Walk / RunのMotion補間に直接使用し、State遷移は
    // Locomotion <-> JumpStart / Fall / LandだけになるためLocomotion境界CrossFadeが不要になります。
    bool BuildCharacterBlendTreeController(
        std::shared_ptr<AnimationClip> idleClip,
        std::shared_ptr<AnimationClip> walkClip,
        std::shared_ptr<AnimationClip> runClip,
        std::shared_ptr<AnimationClip> jumpStartClip,
        std::shared_ptr<AnimationClip> fallClip,
        std::shared_ptr<AnimationClip> landClip,
        const LocomotionBlendTreeSettings& blendSettings = {},
        const JumpStateNames& jumpStateNames = {},
        const CharacterAnimationParameters& parameterNames = {},
        float transitionCrossFadeDuration = 0.15f,
        bool initialGrounded = true,
        bool startImmediately = true);

    // 移動速度からIdle / Walk / Runを選択してTransitionTo()へ変換します。
    // 従来3-State Locomotion向けAPIです。Blend Tree版ではSetFloat(Speed)だけで十分です。
    bool UpdateLocomotion(
        float speed,
        const LocomotionThresholds& thresholds = {},
        const LocomotionStateNames& stateNames = {});

    // Parameter Transitionを評価してからAnimatorを更新し、CrossFade完了をState名へ同期します。
    // BlendTree StateがCurrent / Pendingの場合は、評価前に対応Float ParameterをAnimatorへ同期します。
    void Update(float deltaTime);

    bool HasCurrentState() const { return !m_CurrentStateName.empty(); }
    const std::string& GetCurrentStateName() const { return m_CurrentStateName; }

    // CrossFade中は遷移先State名を返します。遷移中でなければ空文字です。
    const std::string& GetPendingStateName() const { return m_PendingStateName; }

    // Fade中に次の目標Stateが変化した場合の予約先です。
    const std::string& GetQueuedStateName() const { return m_QueuedStateName; }

    Animator& GetAnimator() { return m_Animator; }
    const Animator& GetAnimator() const { return m_Animator; }

private:
    const AnimatorParameter* FindParameter(const std::string& name) const;
    AnimatorParameter* FindParameter(const std::string& name);

    bool GetBlendParameterValue(
        const AnimatorStateDefinition& state,
        float& outValue) const;

    // Current / Pending BlendTree Stateへ最新Parameter値を同期します。
    // Parameter所有権はState Machineに残し、AnimatorにはPose評価に必要な値だけ渡します。
    void SyncAnimatorBlendTreeParameters();

    // 1 Conditionを現在Parameter値に対して評価します。
    bool EvaluateCondition(const AnimatorCondition& condition) const;

    // 現在Stateから出るTransitionを評価し、Priority最大の成立Transitionを実行します。
    bool EvaluateTransitions();

    void ConsumeTransitionTriggers(const AnimatorTransition& transition);

private:
    // Animatorは実際のAnimation再生・時間・Pose評価を担当します。
    // State Machineは所有せず参照するため、AnimatorのLifetimeが本クラスより長い必要があります。
    Animator& m_Animator;

    // State名から定義へ引くRuntimeテーブルです。
    std::unordered_map<std::string, AnimatorStateDefinition> m_States;

    // Parameter値はEntity/Animator Instanceごとに異なるRuntime StateなのでState Machineが保持します。
    std::unordered_map<std::string, AnimatorParameter> m_Parameters;

    // Transitionは登録順も同Priority時のTie Breakとして意味を持つためvectorで保持します。
    std::vector<AnimatorTransition> m_Transitions;

    // CrossFade中もCurrentは遷移元を維持し、Pendingに遷移先を保持します。
    std::string m_CurrentStateName;
    std::string m_PendingStateName;

    // Fade中にさらに目標が変化した場合の「最新1件」の予約です。
    std::string m_QueuedStateName;
};

} // namespace Raven
