#include "Raven/Animation/PoseInertializer.h"

#include <algorithm>
#include <cmath>

namespace Raven
{
namespace
{

// velocity=0の臨界減衰応答 (1 + x) * exp(-x) が1/2になるxです。
// 既存HalfLifeの調整感を大きく変えず、Poseだけの切替でもHalfLife付近で約半分へ収束させます。
constexpr float CriticalHalfLifeDecayConstant = 1.67834699f;
constexpr float RotationVectorEpsilon = 1.0e-6f;

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

float ComputeExponentialDecayWeight(float elapsedTime, float halfLife)
{
    // Scaleは速度を継承しないため、従来どおりHalfLife秒ごとに1/2となる指数減衰を使います。
    return std::pow(0.5f, elapsedTime / halfLife);
}

math::Vec3 QuaternionToRotationVector(const math::Quat& rotation)
{
    math::Quat shortest = rotation.Normalized();

    // qと-qは同じ回転です。w>=0へ揃えることで角度を[0, pi]側へ限定し、
    // 切替時の角速度推定がQuaternion符号反転だけで2*pi近く飛ぶことを防ぎます。
    if (shortest.w < 0.0f)
    {
        shortest = shortest * -1.0f;
    }

    const float vectorLength = std::sqrt(
        shortest.x * shortest.x +
        shortest.y * shortest.y +
        shortest.z * shortest.z);

    if (vectorLength <= RotationVectorEpsilon)
    {
        // sin(theta/2) ~= theta/2 の微小角近似です。
        return math::Vec3{ shortest.x, shortest.y, shortest.z } * 2.0f;
    }

    const float clampedW = std::clamp(shortest.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::atan2(vectorLength, clampedW);
    const float scale = angle / vectorLength;
    return math::Vec3{ shortest.x, shortest.y, shortest.z } * scale;
}

math::Quat RotationVectorToQuaternion(const math::Vec3& rotationVector)
{
    const float angle = rotationVector.Length();
    if (angle <= RotationVectorEpsilon)
    {
        // thetaが十分小さい領域ではsin(theta/2)/theta ~= 1/2です。
        // Identityへ丸めるより微小Offsetを連続に保てるため一次近似を正規化して返します。
        return math::Quat{
            rotationVector.x * 0.5f,
            rotationVector.y * 0.5f,
            rotationVector.z * 0.5f,
            1.0f
        }.Normalized();
    }

    return math::Quat::FromAxisAngle(rotationVector / angle, angle).Normalized();
}

math::Vec3 EstimateAngularVelocity(
    const math::Quat& previousRotation,
    const math::Quat& currentRotation,
    float deltaTime)
{
    // delta = current * previous^-1 とすることで、Local Bone Rotationの変化を
    // parent基準の最短rotation-vectorへ変換します。Euler差分を使わないため+-pi境界を跨げます。
    const math::Quat deltaRotation =
        (currentRotation.Normalized() * previousRotation.Normalized().Inversed()).Normalized();
    return QuaternionToRotationVector(deltaRotation) / deltaTime;
}

math::Vec3 EvaluateCriticalOffset(
    const math::Vec3& initialOffset,
    const math::Vec3& initialVelocity,
    float elapsedTime,
    float halfLife)
{
    // x(t) = (x0 + (v0 + lambda*x0)t) exp(-lambda*t)
    // は臨界減衰系の解析解で、x(0)=x0 と x'(0)=v0 を同時に満たします。
    // Pose差分だけの指数減衰と異なり、切替瞬間のBone速度も初期傾きとして保存できるのが重要です。
    const float decayRate = CriticalHalfLifeDecayConstant / halfLife;
    const float decay = std::exp(-decayRate * elapsedTime);
    const math::Vec3 linearTerm = initialVelocity + initialOffset * decayRate;
    return (initialOffset + linearTerm * elapsedTime) * decay;
}

bool ValidatePose(const SkeletonPose& pose, std::size_t boneCount)
{
    if (pose.GetBoneCount() != boneCount)
    {
        return false;
    }

    for (BoneIndex boneIndex = 0;
        boneIndex < static_cast<BoneIndex>(boneCount);
        ++boneIndex)
    {
        const BoneTransform& transform = pose.GetLocalTransform(boneIndex);
        if (IsFiniteVector(transform.Translation) == false ||
            IsFiniteVector(transform.Scale) == false ||
            IsFiniteQuaternion(transform.Rotation) == false)
        {
            return false;
        }
    }

    return true;
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
    if (ValidatePose(sourcePose, boneCount) == false ||
        ValidatePose(targetPose, boneCount) == false)
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

        BoneOffset& offset = m_Offsets[static_cast<std::size_t>(boneIndex)];
        offset.Translation = source.Translation - target.Translation;
        offset.Scale = source.Scale - target.Scale;

        // Source = RotationOffset * Targetとなる差分をrotation-vectorへ写します。
        // 互換経路では速度履歴が無いため速度差は0から開始します。
        const math::Quat rotationOffset =
            (source.Rotation.Normalized() * target.Rotation.Normalized().Inversed()).Normalized();
        offset.RotationVector = QuaternionToRotationVector(rotationOffset);
    }

    m_ElapsedTime = 0.0f;
    m_Active = true;
    return true;
}

bool PoseInertializer::Begin(
    const Skeleton& skeleton,
    const SkeletonPose& previousSourcePose,
    const SkeletonPose& sourcePose,
    const SkeletonPose& previousTargetPose,
    const SkeletonPose& targetPose,
    float velocityDeltaTime)
{
    Reset();

    if (ValidateConfig(m_Config) == false ||
        velocityDeltaTime <= 0.0f || std::isfinite(velocityDeltaTime) == false)
    {
        return false;
    }

    const std::size_t boneCount = skeleton.GetBoneCount();
    if (ValidatePose(previousSourcePose, boneCount) == false ||
        ValidatePose(sourcePose, boneCount) == false ||
        ValidatePose(previousTargetPose, boneCount) == false ||
        ValidatePose(targetPose, boneCount) == false)
    {
        return false;
    }

    m_Offsets.resize(boneCount);

    for (BoneIndex boneIndex = 0;
        boneIndex < static_cast<BoneIndex>(boneCount);
        ++boneIndex)
    {
        const BoneTransform& previousSource = previousSourcePose.GetLocalTransform(boneIndex);
        const BoneTransform& source = sourcePose.GetLocalTransform(boneIndex);
        const BoneTransform& previousTarget = previousTargetPose.GetLocalTransform(boneIndex);
        const BoneTransform& target = targetPose.GetLocalTransform(boneIndex);

        BoneOffset& offset = m_Offsets[static_cast<std::size_t>(boneIndex)];
        offset.Translation = source.Translation - target.Translation;
        offset.Scale = source.Scale - target.Scale;

        const math::Vec3 sourceLinearVelocity =
            (source.Translation - previousSource.Translation) / velocityDeltaTime;
        const math::Vec3 targetLinearVelocity =
            (target.Translation - previousTarget.Translation) / velocityDeltaTime;
        offset.LinearVelocity = sourceLinearVelocity - targetLinearVelocity;

        const math::Quat rotationOffset =
            (source.Rotation.Normalized() * target.Rotation.Normalized().Inversed()).Normalized();
        offset.RotationVector = QuaternionToRotationVector(rotationOffset);

        const math::Vec3 sourceAngularVelocity = EstimateAngularVelocity(
            previousSource.Rotation,
            source.Rotation,
            velocityDeltaTime);
        const math::Vec3 targetAngularVelocity = EstimateAngularVelocity(
            previousTarget.Rotation,
            target.Rotation,
            velocityDeltaTime);

        // rotationOffsetはTarget orientationをSource orientationへ写すため、Target角速度も
        // 同じSource側のparent基準へ回してから差を取ります。大きなPose差でも単純な成分差より
        // 座標系の不一致を抑えられます。最終的なrotation-vector微分は小さいFrame間回転を前提とします。
        offset.AngularVelocity =
            sourceAngularVelocity - rotationOffset.Rotate(targetAngularVelocity);

        if (IsFiniteVector(offset.LinearVelocity) == false ||
            IsFiniteVector(offset.RotationVector) == false ||
            IsFiniteVector(offset.AngularVelocity) == false)
        {
            Reset();
            return false;
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
    if (ValidatePose(targetPose, boneCount) == false)
    {
        return false;
    }

    if (outPose.GetBoneCount() != boneCount)
    {
        outPose.ResetToBindPose(skeleton);
    }

    float evaluationTime = m_ElapsedTime;
    float scaleDecayWeight = 0.0f;

    if (m_Active == true)
    {
        if (m_Offsets.size() != boneCount)
        {
            return false;
        }

        // 切替FrameはdeltaTime=0なのでSource Poseを厳密に再現し、次FrameからはそのFrame分だけ
        // 先へ進んだ時刻で減衰を評価します。これにより切替Poseを1Frame余分に保持しません。
        evaluationTime += deltaTime;
        scaleDecayWeight = ComputeExponentialDecayWeight(evaluationTime, m_Config.HalfLife);
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

            const math::Vec3 translationCorrection = EvaluateCriticalOffset(
                offset.Translation,
                offset.LinearVelocity,
                evaluationTime,
                m_Config.HalfLife);
            result.Translation = target.Translation + translationCorrection;
            result.Scale = target.Scale + offset.Scale * scaleDecayWeight;

            const math::Vec3 rotationCorrection = EvaluateCriticalOffset(
                offset.RotationVector,
                offset.AngularVelocity,
                evaluationTime,
                m_Config.HalfLife);
            const math::Quat decayedRotation = RotationVectorToQuaternion(rotationCorrection);
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
        m_ElapsedTime = evaluationTime;

        // MaxDurationが指定されている場合は明示的に終了します。
        // 既定値は0なので通常はHalfLife基準の自動終了まで解析減衰を継続します。
        const bool exceededMaxDuration =
            m_Config.MaxDuration > 0.0f &&
            m_ElapsedTime >= m_Config.MaxDuration;

        // 臨界減衰の多項式項も含めて十分小さくなるよう、HalfLifeの10倍を安全な自動終了点とします。
        // 速度差が非常に大きい特殊ケースではMaxDurationを短くせず、Runtime挙動を確認して調整します。
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
