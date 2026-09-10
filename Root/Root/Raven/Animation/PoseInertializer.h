#pragma once

#include "Raven/Animation/SkeletonPose.h"

#include <vector>

namespace Raven
{

// ============================================================================
// PoseInertializerConfig
// ============================================================================
// Motion Matchingの候補切替時に、旧Poseと新Target Poseの差分だけを時間減衰させます。
// CrossFadeのように旧Clipを継続Sampleしないため、切替後は新Clipだけを評価できます。
struct PoseInertializerConfig
{
    // Pose差分が半分になるまでの時間[秒]です。
    // 小さいほど素早く新Poseへ収束します。
    float HalfLife = 0.08f;

    // 長い微小Offsetを残し続けないための任意の最大適用時間[秒]です。
    // 0の場合は強制終了せず、HalfLifeの10倍で残差が1/1024未満になった時点で終了します。
    // 強制終了は微小なPose差を生む可能性があるため既定では無効にしています。
    float MaxDuration = 0.0f;
};

// ============================================================================
// PoseInertializer
// ============================================================================
// 切替直前に表示していたSource Poseと、切替後のTarget Poseとの差分をLocal Bone単位で保持し、
// Target Poseへ加算しながら減衰させるRuntime Stateです。
//
// Rotation Offsetは Source = Offset * Target となるQuaternionとして保持し、
// IdentityへSlerpすることでEuler角へ変換せず最短回転経路で収束させます。
class PoseInertializer
{
public:
    void SetConfig(const PoseInertializerConfig& config) { m_Config = config; }
    const PoseInertializerConfig& GetConfig() const { return m_Config; }

    void Reset();

    // Source Poseを切替瞬間の見た目、Target Poseを新ClipのPoseとしてOffsetを初期化します。
    // Begin直後にApply(..., 0.0f, ...)を呼ぶとSource Poseを再現します。
    bool Begin(
        const Skeleton& skeleton,
        const SkeletonPose& sourcePose,
        const SkeletonPose& targetPose);

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
        math::Vec3 Translation{ 0.0f, 0.0f, 0.0f };
        math::Quat Rotation = math::Quat::Identity();
        math::Vec3 Scale{ 0.0f, 0.0f, 0.0f };
    };

private:
    std::vector<BoneOffset> m_Offsets;
    PoseInertializerConfig m_Config{};
    float m_ElapsedTime = 0.0f;
    bool m_Active = false;
};

} // namespace Raven
