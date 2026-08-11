#pragma once

#include "Raven/Animation/AnimationClip.h"
#include "Raven/Animation/AnimatorState.h"
#include "Raven/Animation/BlendTree1D.h"
#include "Raven/Animation/PoseBlending.h"

#include <memory>

namespace Raven
{

// ============================================================================
// Animator
// ============================================================================
// AnimationClip / BlendTree1Dが「再生されるMotionデータ」であるのに対し、Animatorは
// 「そのMotionを現在どのように再生しているか」というruntime stateを所有します。
//
// CrossFade中はCurrentState / NextStateを同時に進め、それぞれのPoseを評価した後、
// Fade Weightで補間します。ClipとBlendTreeを同じAnimatorStateとして扱うため、
// Locomotion Blend Tree -> JumpStart Clipのような異種Motion間も同じCrossFade経路を使えます。
//
// 単一Transform AnimationとSkeletal Animationは同じ再生時刻/Stateを共有します。
// SkeletonをSetSkeleton()で接続した場合は、TransformPoseに加えてSkeletonPoseも
// 同じCurrent/Next Stateから評価されます。
class Animator
{
public:
    // ClipをCurrent Motionとして再生します。
    // restart=falseかつ同じClipの場合は現在位相を維持し、別Motionから切り替わる場合は
    // Motionの種類が変わるため先頭（逆再生時は末尾）から開始します。
    void Play(std::shared_ptr<AnimationClip> clip, bool restart = true);

    // BlendTree1DをCurrent Motionとして再生します。
    // BlendParameterはState Machineが持つSpeedなどのFloat Parameter値を同期して使用します。
    // Blend Tree自身はRuntime時間を持たず、AnimatorStateのNormalizedTimeを共有再生位相として使います。
    void PlayBlendTree(
        std::shared_ptr<BlendTree1D> blendTree,
        float blendParameter,
        bool restart = true);

    // 現在Motionからtarget Clipへduration秒かけて滑らかに遷移します。
    // まずは1段CrossFadeに限定し、Fade中の再CrossFadeはfalseを返します。
    // 現在のBlend済みPoseをSnapshotとして保持できない段階で割り込みを許可するとPoseが跳ぶため、
    // 複雑なInterrupt TransitionはSnapshot Poseを導入した段階で別ポリシーとして追加します。
    bool CrossFade(std::shared_ptr<AnimationClip> clip, float duration, bool restart = true);

    // 現在MotionからBlendTree1DへCrossFadeします。
    // Clip -> BlendTree / BlendTree -> BlendTreeの両方を同じPose Blend経路で扱います。
    // CrossFade中の再割り込みを拒否するルールはClip版CrossFadeと共通です。
    bool CrossFadeBlendTree(
        std::shared_ptr<BlendTree1D> blendTree,
        float blendParameter,
        float duration,
        bool restart = true);

    // Current / NextがBlendTreeの場合にParameter値だけを更新します。
    // 再生位相は維持されるため、Speed変化によってClip選択が変わっても歩行周期は連続します。
    bool SetCurrentBlendParameter(float value);
    bool SetNextBlendParameter(float value);

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
    float GetNormalizedTime() const { return m_CurrentState.NormalizedTime; }

    bool IsPlaying() const { return m_Playing; }
    bool IsPaused() const { return m_Paused; }

    // 非Loop Motionが再生方向側の端へ到達したかを示します。
    // IsPlaying()だけではPause/Stopとの区別が付かないため、State Machineから
    // 「Motion終了を遷移条件にする」場合にも利用できる専用状態として保持します。
    // CrossFade完了時は、遷移先Motionが終端へ到達していた場合にtrueになります。
    bool IsFinished() const { return m_Finished; }

    bool IsCrossFading() const { return m_CrossFading; }

    float GetCrossFadeWeight() const;

    const TransformPose& GetCurrentPose() const { return m_CurrentPose; }

    // Clip再生時だけ有効な互換APIです。Current MotionがBlendTreeの場合はnullptrを返します。
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
    // CrossFade中はCurrent/Next MotionのSkeletonPoseをBlendPoses()で補間した結果です。
    const SkeletonPose& GetCurrentSkeletonPose() const { return m_CurrentSkeletonPose; }

private:
    // Stateの再生時間/位相だけを進めます。
    // ClipではClip Duration、BlendTreeでは現在Parameter位置の補間Durationを1周期長として扱います。
    // 非Loop Motionで再生方向側の端へ到達した場合はtrueを返します。
    bool AdvanceState(AnimatorState& state, float dt);

    // Motion種類に応じてDurationを取得します。
    // BlendTreeでは現在Parameter値における隣接Child Durationの補間値を返します。
    // Parameterが変わってDurationが変化してもNormalizedTimeは維持し、歩行周期の位相を保ちます。
    float GetStateDuration(const AnimatorState& state) const;

    // AnimatorStateのMotion種類を隠蔽して最終PoseをSampleする共通入口です。
    // EvaluateCurrentPose()はClip / BlendTreeを個別判定せず、この2関数の結果だけをCrossFadeします。
    bool SampleTransformPose(const AnimatorState& state, TransformPose& outPose) const;
    bool SampleSkeletonPose(const AnimatorState& state, SkeletonPose& outPose) const;

    // CurrentState単体、またはCrossFade中のCurrent/Next両方を評価します。
    // Skeleton接続済みの場合はSkeletonPoseも同じState/Weightで同時に評価します。
    void EvaluateCurrentPose();

    // Fade完了時にNextStateをCurrentStateへ昇格します。
    // AnimatorStateごと昇格するためClip/BlendTree種別・再生位相・BlendParameterも途切れません。
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

    // CrossFadeでは2つのMotionを同時Sampleするため、一時PoseをAnimator内部へ保持します。
    // m_CurrentSkeletonPoseが最終的にRenderer/Skinningへ渡す出力Poseです。
    SkeletonPose m_CurrentSkeletonPose{};
    SkeletonPose m_NextSkeletonPose{};
};

} // namespace Raven
