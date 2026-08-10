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
// AnimatorStateMachine
// ============================================================================
// Animatorの上に置く薄いState管理層です。
//
// 責務は次の3つだけに限定します。
//  1. "Idle" / "Walk" / "Run" のような名前とAnimationClipを対応付ける
//  2. 現在State名を管理する
//  3. State変更要求をAnimator::Play / CrossFadeへ変換する
//
// Animation時間、Loop、Pose Sample、CrossFade WeightなどのRuntime処理はAnimator側へ残します。
// これによりState MachineがAnimation再生処理を二重管理しない構造になります。
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

    bool RemoveState(const std::string& name);
    bool HasState(const std::string& name) const;

    const AnimatorStateDefinition* FindState(const std::string& name) const;

    // 初期Stateを設定します。
    // startImmediately=trueならAnimator::Play()まで行い、そのStateを現在Stateにします。
    bool SetInitialState(const std::string& name, bool startImmediately = true);

    // 登録済みStateへ遷移します。
    // durationOverride < 0 の場合はState定義のCrossFadeDurationを使用します。
    // AnimatorがCrossFade中で割り込みを拒否した場合、State名も変更しません。
    bool TransitionTo(const std::string& name, float durationOverride = -1.0f);

    // Animatorを更新した後、CrossFade完了をState名へ同期します。
    // State Machine自身はAnimation時間を別途持たず、時間管理はAnimatorだけに任せます。
    void Update(float deltaTime);

    bool HasCurrentState() const { return !m_CurrentStateName.empty(); }
    const std::string& GetCurrentStateName() const { return m_CurrentStateName; }

    // CrossFade中は遷移先State名を返します。遷移中でなければ空文字です。
    const std::string& GetPendingStateName() const { return m_PendingStateName; }

    Animator& GetAnimator() { return m_Animator; }
    const Animator& GetAnimator() const { return m_Animator; }

private:
    Animator& m_Animator;
    std::unordered_map<std::string, AnimatorStateDefinition> m_States;

    std::string m_CurrentStateName;
    std::string m_PendingStateName;
};

} // namespace Raven
