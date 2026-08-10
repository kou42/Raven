#pragma once

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Animation/AnimatorState.h"
#include "Raven/Animation/PoseBlending.h"

#include <memory>

namespace Raven
{

// ============================================================================
// Animator
// ============================================================================
// AnimationClipが「再生されるデータ」であるのに対し、Animatorは
// 「そのClipを現在どのように再生しているか」というruntime stateを所有します。
//
// CrossFade中はCurrentState / NextStateを同時に進め、それぞれのPoseを評価した後、
// Fade Weightで補間します。State Machineを追加する際も、この2-State遷移を
// そのままIdle -> Walk -> RunなどのTransition実行部として再利用できます。
//
// 単一Transform AnimationとSkeletal Animationは同じ再生時刻/Stateを共有します。
// SkeletonをSetSkeleton()で接続した場合は、TransformPoseに加えてSkeletonPoseも
// 同じCurrent/Next Stateから評価されます。
class Animator
{
public:
    void Play(std::shared_ptr<AnimationClip> clip, bool restart = true);

    // 現在Clipからtarget Clipへduration秒かけて滑らかに遷移します。
    // まずは1段CrossFadeに限定し、Fade中の再CrossFadeはfalseを返します。
    // 複雑なInterrupt TransitionはState Machine実装時に別ポリシーとして追加します。
    bool CrossFade(std::shared_ptr<AnimationClip> clip, float duration, bool restart = true);

    void Pause();
    void Resume();
    void Stop();

    void Update(float dt);

    void SetLoop(bool loop);
    bool IsLooping() const { return m_Loop; }

    void SetSpeed(float speed) { m_Speed = speed; }
    float GetSpeed() const { return m_Speed; }

    // Timeline操作用APIです。
    // Editorのscrub、State遷移開始位置、同期AnimationなどでAnimator::Update()を
    // 経由せず任意時刻へ移動したい場合に使用します。
    //
    // CrossFade中に呼ばれた場合は「Current Stateへの明示的なTimeline操作」と解釈し、
    // Next StateとFade状態を破棄してCurrent State単体へ戻します。
    void SetCurrentTime(float time);
    void SetNormalizedTime(float normalizedTime);

    float GetCurrentTime() const { return m_CurrentState.Time; }
    float GetNormalizedTime() const;

    bool IsPlaying() const { return m_Playing; }
    bool IsPaused() const { return m_Paused; }

    // 非Loop Clipが再生方向側の端へ到達したかを示します。
    // IsPlaying()だけではPause/Stopとの区別が付かないため、State Machine実装時に
    // 「Animation終了を遷移条件にする」ための専用状態として保持します。
    // CrossFade完了時は、遷移先Stateが終端へ到達していた場合にtrueになります。
    bool IsFinished() const { return m_Finished; }

    bool IsCrossFading() const { return m_CrossFading; }

    float GetCrossFadeWeight() const;

    const TransformPose& GetCurrentPose() const { return m_CurrentPose; }
    const std::shared_ptr<AnimationClip>& GetClip() const { return m_CurrentState.Clip; }
    const AnimatorState& GetCurrentState() const { return m_CurrentState; }
    const AnimatorState& GetNextState() const { return m_NextState; }

    // ------------------------------------------------------------------------
    // Skeletal Animation
    // ------------------------------------------------------------------------
    // AnimatorへSkeleton定義を接続します。Skeleton自体の所有権はAnimatorへ移しません。
    // Mesh/Deformerなどの共有定義データ側がAnimatorより長く生存することが前提です。
    // nullptrを渡すとSkeletal Animation評価を解除します。
    void SetSkeleton(const Skeleton* skeleton);

    const Skeleton* GetSkeleton() const { return m_Skeleton; }
    bool HasSkeleton() const { return m_Skeleton != nullptr; }

    // SetSkeleton()済みの場合に、現在のAnimation結果として評価された全Bone Poseを返します。
    // CrossFade中はCurrent/Next ClipのSkeletonPoseをBlendPoses()で補間した結果です。
    const SkeletonPose& GetCurrentSkeletonPose() const { return m_CurrentSkeletonPose; }

private:
    // StateのTimeだけを進めます。
    // 非Loopで終端へ達した場合はtrueを返します。
    bool AdvanceState(AnimatorState& state, float dt);

    // CurrentState単体、またはCrossFade中のCurrent/Next両方を評価します。
    // Skeleton接続済みの場合はSkeletonPoseも同じState/Weightで同時に評価します。
    void EvaluateCurrentPose();

    // Fade完了時にNextStateをCurrentStateへ昇格します。
    void CompleteCrossFade();

private:
    AnimatorState m_CurrentState{};
    AnimatorState m_NextState{};

    float m_Speed = 1.0f;

    bool m_Playing = false;
    bool m_Paused = false;
    bool m_Loop = true;
    bool m_Finished = false;

    bool m_CrossFading = false;
    float m_FadeElapsed = 0.0f;
    float m_FadeDuration = 0.0f;

    TransformPose m_CurrentPose{};

    // SkeletonはAsset/Deformer側の共有定義を参照するだけで所有しません。
    const Skeleton* m_Skeleton = nullptr;

    // CrossFadeでは2つのClipを同時Sampleするため、一時PoseをAnimator内部へ保持します。
    // m_CurrentSkeletonPoseが最終的にRenderer/Skinningへ渡す出力Poseです。
    SkeletonPose m_CurrentSkeletonPose{};
    SkeletonPose m_NextSkeletonPose{};
};

} // namespace Raven
