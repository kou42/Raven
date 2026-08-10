// Raven/Animation/AnimatorStateMachine.h
#pragma once

#include "Raven/Animation/Animator.h"

#include <memory>
#include <string>
#include <unordered_map>

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

// ============================================================================
// AnimatorStateMachine
// ============================================================================
// Animatorの上に置く薄いState管理層です。
//
// 責務は次の3つを中心に限定します。
//  1. "Idle" / "Walk" / "Run" のような名前とAnimationClipを対応付ける
//  2. Current / Pending / QueuedのState名を管理する
//  3. State変更要求をAnimator::Play / CrossFadeへ変換する
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
    bool AddState(
        std::string name,
        std::shared_ptr<AnimationClip> clip,
        float crossFadeDuration = 0.2f,
        bool restartOnEnter = true);

    // 登録済みStateを削除します。
    // Current / Pending / Queuedに該当する名前だった場合は対応する管理名も解除します。
    bool RemoveState(const std::string& name);
    bool HasState(const std::string& name) const;

    // State定義への参照を取得します。未登録の場合はnullptrです。
    const AnimatorStateDefinition* FindState(const std::string& name) const;

    // 初期Stateを設定します。
    // startImmediately=trueならAnimator::Play()まで行い、そのStateを現在Stateにします。
    bool SetInitialState(const std::string& name, bool startImmediately = true);

    // 登録済みStateへ遷移します。
    // durationOverride < 0 の場合はState定義のCrossFadeDurationを使用します。
    // AnimatorがCrossFade中で割り込みを拒否した場合、State名も変更しません。
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

    // Animatorを更新した後、CrossFade完了をState名へ同期します。
    // State Machine自身はAnimation時間を別途持たず、時間管理はAnimatorだけに任せます。
    // Fade中にQueued Stateがあれば、完了直後に次のTransitionとして適用します。
    void Update(float deltaTime);

    bool HasCurrentState() const { return !m_CurrentStateName.empty(); }
    const std::string& GetCurrentStateName() const { return m_CurrentStateName; }

    // CrossFade中は遷移先State名を返します。遷移中でなければ空文字です。
    const std::string& GetPendingStateName() const { return m_PendingStateName; }

    // Fade中に次の目標Stateが変化した場合の予約先です。
    // 例: Idle -> WalkのFade中に速度がRun領域へ入った場合、Runをここへ保持します。
    const std::string& GetQueuedStateName() const { return m_QueuedStateName; }

    Animator& GetAnimator() { return m_Animator; }
    const Animator& GetAnimator() const { return m_Animator; }

private:
    // Animatorは実際のAnimation再生・時間・Pose評価を担当します。
    // State Machineは所有せず参照するため、AnimatorのLifetimeが本クラスより長い必要があります。
    Animator& m_Animator;

    // State名から定義へ引くRuntimeテーブルです。
    std::unordered_map<std::string, AnimatorStateDefinition> m_States;

    // CrossFade中もCurrentは遷移元を維持し、Pendingに遷移先を保持します。
    // Fade完了時にPending -> Currentへ昇格します。
    std::string m_CurrentStateName;
    std::string m_PendingStateName;

    // Fade中にさらに目標が変わった場合の「最新1件」の予約です。
    std::string m_QueuedStateName;
};

} // namespace Raven
