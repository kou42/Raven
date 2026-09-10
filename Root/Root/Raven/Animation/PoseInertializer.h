#pragma once

#include "Raven/Animation/SkeletonPose.h"

#include <vector>

namespace Raven
{

// ============================================================================
// PoseInertializerConfig
// ============================================================================
// Motion Matchingの候補切替時に、旧出力と新TargetのPose差分・速度差分を減衰させます。
// CrossFadeのように旧Clipを継続Sampleしないため、切替後は新Clipだけを評価できます。
struct PoseInertializerConfig
{
    // 慣性Offsetが収束する時間Scale[秒]です。
    // 位置/回転の速度差が0の場合、臨界減衰Offsetがおおむねこの時間で1/2になります。
    // 小さいほど素早く新Poseへ収束します。
    float HalfLife = 0.08f;

    // 長い微小Offsetを残し続けないための任意の最大適用時間[秒]です。
    // 0の場合は強制終了せず、HalfLifeの10倍で十分減衰した時点で終了します。
    // 強制終了は微小なPose差を生む可能性があるため既定では無効にしています。
    float MaxDuration = 0.0f;
};

// ============================================================================
// PoseInertializer
// ============================================================================
// 切替直前に表示していたSource Poseと切替後Target Poseとの差分をLocal Bone単位で保持し、
// Target Poseへ加算しながら減衰させるRuntime Stateです。
//
// 高次経路では直前Frameも受け取り、Translationの線形速度とRotationの角速度を推定します。
// 切替瞬間のPose差分だけでなく速度差分も臨界減衰の初期条件へ入れるため、歩行中のFootや
// 腕振りのようにBoneが動いている最中でも、切替Frame直後に速度が急変しにくくなります。
//
// RotationはQuaternionのまま成分減算せず、最短回転をrotation-vectorへ写して減衰します。
// これによりEuler角のwrapやQuaternionのq/-q表現差へ依存せず角速度を扱えます。
class PoseInertializer
{
public:
    void SetConfig(const PoseInertializerConfig& config) { m_Config = config; }
    const PoseInertializerConfig& GetConfig() const { return m_Config; }

    void Reset();

    // Pose差分だけを使う互換経路です。比較用の前Frameが無い初期状態などで使用します。
    // Begin直後にApply(..., 0.0f, ...)を呼ぶとSource Poseを再現します。
    bool Begin(
        const Skeleton& skeleton,
        const SkeletonPose& sourcePose,
        const SkeletonPose& targetPose);

    // Pose差分に加えて、source/targetそれぞれの直前FrameからBone線形速度・角速度を推定します。
    // velocityDeltaTimeは両Pose履歴の時間間隔で、0以下や非有限値は拒否します。
    bool Begin(
        const Skeleton& skeleton,
        const SkeletonPose& previousSourcePose,
        const SkeletonPose& sourcePose,
        const SkeletonPose& previousTargetPose,
        const SkeletonPose& targetPose,
        float velocityDeltaTime);

    // deltaTime分だけ減衰を進めたOffsetをTarget Poseへ重ねて返します。
    // Begin直後の切替FrameだけdeltaTime=0で呼ぶことでSource Poseを厳密に再現できます。
    // Activeでない場合はTarget Poseをそのままコピーします。
    bool Apply(
        const Skeleton& skeleton,
        const SkeletonPose& targetPose,
        float deltaTime,
        SkeletonPose& outPose);

    bool IsActive() const { return m_Active; }
    float GetElapsedTime() const { return m_ElapsedTime; }

private:
    struct BoneOffset
    {
        // Source - TargetのLocal Translation差分と、その時間微分の差分です。
        math::Vec3 Translation{ 0.0f, 0.0f, 0.0f };
        math::Vec3 LinearVelocity{ 0.0f, 0.0f, 0.0f };

        // Source = RotationOffset * Target のRotationOffsetをrotation-vectorで保持します。
        // AngularVelocityは同じLocal/parent基準で推定した初期角速度差です。[rad/s]
        math::Vec3 RotationVector{ 0.0f, 0.0f, 0.0f };
        math::Vec3 AngularVelocity{ 0.0f, 0.0f, 0.0f };

        // Scaleは速度継承の対象外とし、従来どおりPose差分だけを指数減衰します。
        math::Vec3 Scale{ 0.0f, 0.0f, 0.0f };
    };

private:
    std::vector<BoneOffset> m_Offsets;
    PoseInertializerConfig m_Config{};
    float m_ElapsedTime = 0.0f;
    bool m_Active = false;
};

} // namespace Raven
