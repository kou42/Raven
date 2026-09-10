#include "Raven/Animation/PoseInertializer.h"

#include <algorithm>
#include <cmath>

namespace Raven
{
namespace
{

bool IsFiniteVector(const math::Vec3& value)
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool IsFiniteQuaternion(const math::Quat& value)
{
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z) &&
        std::isfinite(value.w);
}

bool ValidateConfig(const PoseInertializerConfig& config)
{
    if (config.HalfLife <= 0.0f || std::isfinite(config.HalfLife) == false)
    {
        return false;
    }

    if (config.MaxDuration < 0.0f || std::isfinite(config.MaxDuration) == false)
    {
        return false;
    }

    return true;
}

float ComputeDecayWeight(float elapsedTime, float halfLife)
{
    // HalfLife秒ごとに差分が1/2になる指数減衰です。
    // pow(0.5, t / halfLife)を使うことで調整値の意味を直感的に保ちます。
    return std::pow(0.5f, elapsedTime / halfLife);
}

} // namespace

void PoseInertializer::Reset()
{
    m_Offsets.clear();
    m_ElapsedTime = 0.0f;
    m_Active = false;
}

bool PoseInertializer::Begin(
    const Skeleton& skeleton,
    const SkeletonPose& sourcePose,
    const SkeletonPose& targetPose)
{
    Reset();

    if (ValidateConfig(m_Config) == false)
    {
        return false;
    }

    const std::size_t boneCount = skeleton.GetBoneCount();
    if (sourcePose.GetBoneCount() != boneCount ||
        targetPose.GetBoneCount() != boneCount)
    {
        return false;
    }

    m_Offsets.resize(boneCount);

    for (BoneIndex boneIndex = 0;
        boneIndex < static_cast<BoneIndex>(boneCount);
        ++boneIndex)
    {
        const BoneTransform& source = sourcePose.GetLocalTransform(boneIndex);
        const BoneTransform& target = targetPose.GetLocalTransform(boneIndex);

        if (IsFiniteVector(source.Translation) == false ||
            IsFiniteVector(source.Scale) == false ||
            IsFiniteQuaternion(source.Rotation) == false ||
            IsFiniteVector(target.Translation) == false ||
            IsFiniteVector(target.Scale) == false ||
            IsFiniteQuaternion(target.Rotation) == false)
        {
            Reset();
            return false;
        }

        BoneOffset& offset = m_Offsets[static_cast<std::size_t>(boneIndex)];
        offset.Translation = source.Translation - target.Translation;
        offset.Scale = source.Scale - target.Scale;

        // Source = RotationOffset * Target となる差分Quaternionを保持します。
        // Quaternion符号はqと-qが同じ回転を表すため、IdentityとのDotが負なら反転して
        // 後続Slerpが不要に長い経路を選ばないようにします。
        offset.Rotation =
            (source.Rotation.Normalized() * target.Rotation.Normalized().Inversed()).Normalized();

        if (math::Quat::Dot(math::Quat::Identity(), offset.Rotation) < 0.0f)
        {
            offset.Rotation = offset.Rotation * -1.0f;
        }
    }

    m_ElapsedTime = 0.0f;
    m_Active = true;
    return true;
}

bool PoseInertializer::Apply(
    const Skeleton& skeleton,
    const SkeletonPose& targetPose,
    float deltaTime,
    SkeletonPose& outPose)
{
    if (ValidateConfig(m_Config) == false ||
        deltaTime < 0.0f || std::isfinite(deltaTime) == false)
    {
        return false;
    }

    const std::size_t boneCount = skeleton.GetBoneCount();
    if (targetPose.GetBoneCount() != boneCount)
    {
        return false;
    }

    if (outPose.GetBoneCount() != boneCount)
    {
        outPose.ResetToBindPose(skeleton);
    }

    float decayWeight = 0.0f;
    if (m_Active == true)
    {
        if (m_Offsets.size() != boneCount)
        {
            return false;
        }

        decayWeight = ComputeDecayWeight(m_ElapsedTime, m_Config.HalfLife);
    }

    for (BoneIndex boneIndex = 0;
        boneIndex < static_cast<BoneIndex>(boneCount);
        ++boneIndex)
    {
        const BoneTransform& target = targetPose.GetLocalTransform(boneIndex);
        BoneTransform result = target;

        if (m_Active == true)
        {
            const BoneOffset& offset = m_Offsets[static_cast<std::size_t>(boneIndex)];

            result.Translation = target.Translation + offset.Translation * decayWeight;
            result.Scale = target.Scale + offset.Scale * decayWeight;

            const math::Quat decayedRotation = math::Quat::Slerp(
                math::Quat::Identity(),
                offset.Rotation,
                decayWeight);
            result.Rotation = (decayedRotation * target.Rotation).Normalized();
        }

        if (outPose.SetLocalTransform(boneIndex, result) == false)
        {
            return false;
        }
    }

    if (outPose.UpdateGlobalTransforms(skeleton) == false)
    {
        return false;
    }

    if (m_Active == true)
    {
        m_ElapsedTime += deltaTime;

        // MaxDurationが指定されている場合は明示的に終了します。
        // 指数減衰は理論上0にならないため、Runtimeで永続的にActiveを維持しないための上限です。
        const bool exceededMaxDuration =
            m_Config.MaxDuration > 0.0f &&
            m_ElapsedTime >= m_Config.MaxDuration;

        // 半減期の約10回分で残差は1/1024未満なので、MaxDuration=0でも十分小さくなったら終了します。
        const bool sufficientlyDecayed =
            m_ElapsedTime >= (m_Config.HalfLife * 10.0f);

        if (exceededMaxDuration == true || sufficientlyDecayed == true)
        {
            Reset();
        }
    }

    return true;
}

} // namespace Raven
