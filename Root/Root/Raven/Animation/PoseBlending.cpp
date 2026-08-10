// Raven/Animation/PoseBlending.cpp
#include "Raven/Animation/PoseBlending.h"

#include <algorithm>

namespace Raven
{
namespace
{
math::Vec3 Lerp(const math::Vec3& a, const math::Vec3& b, float t)
{
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

BoneTransform BlendBoneTransform(
    const BoneTransform& from,
    const BoneTransform& to,
    float weight)
{
    BoneTransform result{};

    // Translation / Scaleは線形空間なのでLerpします。
    result.Translation = Lerp(from.Translation, to.Translation, weight);
    result.Scale = Lerp(from.Scale, to.Scale, weight);

    // RotationをEuler角や行列で補間すると回転経路が不自然になりやすいため、
    // QuaternionのSlerpを使用します。
    // MathQuatanion::Slerp()側で最短経路と正規化を扱うため、CrossFade側は
    // 「0 -> 1のBlend Weight」を渡すだけでBone回転を自然につなげられます。
    result.Rotation = math::Quat::Slerp(from.Rotation, to.Rotation, weight);

    return result;
}
} // namespace

bool BlendPoses(
    const Skeleton& skeleton,
    const SkeletonPose& from,
    const SkeletonPose& to,
    float weight,
    SkeletonPose& result)
{
    const std::size_t boneCount = skeleton.GetBoneCount();

    // PoseはSkeletonと1対1対応です。
    // 異なるSkeleton由来のPoseをBlendするとBone Indexの意味が変わるため、
    // サイズが一致しない場合は補間せず明示的に失敗させます。
    if (from.GetBoneCount() != boneCount || to.GetBoneCount() != boneCount)
        return false;

    // CrossFade開始直後/終了直後も同じ処理経路を通せるようClampします。
    // durationが非常に短い場合などにweightが僅かに範囲外へ出てもPoseを壊しません。
    const float t = std::clamp(weight, 0.0f, 1.0f);

    // resultが未初期化でも利用できるようSkeletonサイズへ初期化します。
    // このBind Pose値は直後に全Bone上書きされますが、SkeletonPoseの内部配列を
    // 安全に確保し、SetLocalTransform()を正規経路で使用するために必要です。
    if (result.GetBoneCount() != boneCount)
        result.ResetToBindPose(skeleton);

    for (BoneIndex i = 0; i < static_cast<BoneIndex>(boneCount); ++i)
    {
        const BoneTransform blended = BlendBoneTransform(
            from.GetLocalTransform(i),
            to.GetLocalTransform(i),
            t);

        if (!result.SetLocalTransform(i, blended))
            return false;
    }

    // Skinningで必要なのはGlobal Poseなので、全Local Transformを書き終えた後に
    // 一度だけ階層変換を再構築します。Boneごとに更新しないことが重要です。
    return result.UpdateGlobalTransforms(skeleton);
}

} // namespace Raven
