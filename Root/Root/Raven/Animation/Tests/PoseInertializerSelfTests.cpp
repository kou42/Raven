#include "Raven/Animation/Tests/PoseInertializerSelfTests.h"

#include "Raven/Animation/PoseInertializer.h"

#include <cassert>
#include <cmath>

namespace Raven::tests
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;

bool NearlyEqual(float a, float b, float tolerance = 1.0e-3f)
{
    return std::abs(a - b) <= tolerance;
}

Skeleton CreateOneBoneSkeleton(BoneIndex& outRootIndex)
{
    Skeleton skeleton;
    Bone root{};
    root.Name = "Root";
    outRootIndex = skeleton.AddBone(root);
    assert(outRootIndex != InvalidBoneIndex);
    return skeleton;
}

SkeletonPose CreatePose(
    const Skeleton& skeleton,
    BoneIndex rootIndex,
    const math::Vec3& translation,
    const math::Quat& rotation)
{
    SkeletonPose pose;
    pose.ResetToBindPose(skeleton);

    BoneTransform transform = pose.GetLocalTransform(rootIndex);
    transform.Translation = translation;
    transform.Rotation = rotation;
    assert(pose.SetLocalTransform(rootIndex, transform));
    assert(pose.UpdateGlobalTransforms(skeleton));
    return pose;
}

float ExtractZRotation(const math::Quat& rotation)
{
    // Unit Xを回してatan2を取ることで、Quaternion符号に依存せずZ回転角を取得します。
    const math::Vec3 rotatedX = rotation.Normalized().Rotate({ 1.0f, 0.0f, 0.0f });
    return std::atan2(rotatedX.y, rotatedX.x);
}

void RunVelocityAwareTranslationTest()
{
    BoneIndex rootIndex = InvalidBoneIndex;
    const Skeleton skeleton = CreateOneBoneSkeleton(rootIndex);

    constexpr float historyDeltaTime = 0.1f;
    constexpr float sourceVelocity = 1.0f;

    const SkeletonPose previousSource = CreatePose(
        skeleton, rootIndex, { 0.0f, 0.0f, 0.0f }, math::Quat::Identity());
    const SkeletonPose source = CreatePose(
        skeleton, rootIndex, { 0.1f, 0.0f, 0.0f }, math::Quat::Identity());
    const SkeletonPose previousTarget = CreatePose(
        skeleton, rootIndex, { 1.0f, 0.0f, 0.0f }, math::Quat::Identity());
    const SkeletonPose target = previousTarget;

    PoseInertializer inertializer;
    assert(inertializer.Begin(
        skeleton,
        previousSource,
        source,
        previousTarget,
        target,
        historyDeltaTime));

    PoseInertializerBoneDebugInfo debugInfo{};
    assert(inertializer.GetBoneDebugInfo(rootIndex, debugInfo));
    assert(NearlyEqual(debugInfo.InitialLinearVelocityError.x, sourceVelocity));
    assert(NearlyEqual(debugInfo.InitialLinearVelocityError.y, 0.0f));
    assert(NearlyEqual(debugInfo.InitialLinearVelocityError.z, 0.0f));

    SkeletonPose switchPose;
    assert(inertializer.Apply(skeleton, target, 0.0f, switchPose));

    // 切替FrameではTargetではなく直前Sourceを厳密に再現し、Pose popを防ぎます。
    assert(NearlyEqual(
        switchPose.GetLocalTransform(rootIndex).Translation.x,
        source.GetLocalTransform(rootIndex).Translation.x,
        1.0e-5f));

    // 十分小さいdtで次Frameを評価し、有限差分した出力速度が切替直前速度へ接続することを確認します。
    // 臨界減衰解析解はx'(0)=初期速度Errorを満たすため、Targetが静止なら出力速度はSource速度になります。
    constexpr float probeDeltaTime = 1.0e-4f;
    SkeletonPose nextPose;
    assert(inertializer.Apply(skeleton, target, probeDeltaTime, nextPose));

    const float measuredVelocity =
        (nextPose.GetLocalTransform(rootIndex).Translation.x -
            switchPose.GetLocalTransform(rootIndex).Translation.x) /
        probeDeltaTime;
    assert(NearlyEqual(measuredVelocity, sourceVelocity, 2.0e-2f));
}

void RunVelocityAwareRotationTest()
{
    BoneIndex rootIndex = InvalidBoneIndex;
    const Skeleton skeleton = CreateOneBoneSkeleton(rootIndex);

    constexpr float historyDeltaTime = 0.1f;
    constexpr float sourceAngularVelocity = 1.0f;
    constexpr float sourceAngle = sourceAngularVelocity * historyDeltaTime;

    const SkeletonPose previousSource = CreatePose(
        skeleton, rootIndex, {}, math::Quat::Identity());
    const SkeletonPose source = CreatePose(
        skeleton,
        rootIndex,
        {},
        math::Quat::FromAxisAngle({ 0.0f, 0.0f, 1.0f }, sourceAngle));
    const SkeletonPose previousTarget = CreatePose(
        skeleton, rootIndex, {}, math::Quat::Identity());
    const SkeletonPose target = previousTarget;

    PoseInertializer inertializer;
    assert(inertializer.Begin(
        skeleton,
        previousSource,
        source,
        previousTarget,
        target,
        historyDeltaTime));

    PoseInertializerBoneDebugInfo debugInfo{};
    assert(inertializer.GetBoneDebugInfo(rootIndex, debugInfo));
    assert(NearlyEqual(debugInfo.InitialAngularVelocityError.z, sourceAngularVelocity, 1.0e-3f));

    SkeletonPose switchPose;
    assert(inertializer.Apply(skeleton, target, 0.0f, switchPose));
    const float switchAngle = ExtractZRotation(switchPose.GetLocalTransform(rootIndex).Rotation);
    assert(NearlyEqual(switchAngle, sourceAngle, 1.0e-4f));

    constexpr float probeDeltaTime = 1.0e-4f;
    SkeletonPose nextPose;
    assert(inertializer.Apply(skeleton, target, probeDeltaTime, nextPose));
    const float nextAngle = ExtractZRotation(nextPose.GetLocalTransform(rootIndex).Rotation);
    const float measuredAngularVelocity = (nextAngle - switchAngle) / probeDeltaTime;
    assert(NearlyEqual(measuredAngularVelocity, sourceAngularVelocity, 2.0e-2f));
}

void RunQuaternionSignContinuityTest()
{
    BoneIndex rootIndex = InvalidBoneIndex;
    const Skeleton skeleton = CreateOneBoneSkeleton(rootIndex);

    constexpr float historyDeltaTime = 0.1f;
    const math::Quat rotation = math::Quat::FromAxisAngle(
        { 0.0f, 1.0f, 0.0f },
        Pi * 0.25f).Normalized();
    const math::Quat equivalentNegative = rotation * -1.0f;

    const SkeletonPose previousSource = CreatePose(skeleton, rootIndex, {}, rotation);
    const SkeletonPose source = CreatePose(skeleton, rootIndex, {}, equivalentNegative);
    const SkeletonPose target = CreatePose(skeleton, rootIndex, {}, rotation);

    PoseInertializer inertializer;
    assert(inertializer.Begin(
        skeleton,
        previousSource,
        source,
        target,
        target,
        historyDeltaTime));

    PoseInertializerBoneDebugInfo debugInfo{};
    assert(inertializer.GetBoneDebugInfo(rootIndex, debugInfo));

    // qと-qは同一姿勢なので、Quaternion符号反転だけでは角速度Errorを発生させません。
    assert(debugInfo.InitialAngularVelocityError.Length() <= 1.0e-4f);
    assert(debugInfo.InitialRotationOffset.Length() <= 1.0e-4f);
}

void RunInvalidVelocityDeltaTimeTest()
{
    BoneIndex rootIndex = InvalidBoneIndex;
    const Skeleton skeleton = CreateOneBoneSkeleton(rootIndex);
    const SkeletonPose pose = CreatePose(skeleton, rootIndex, {}, math::Quat::Identity());

    PoseInertializer inertializer;
    assert(inertializer.Begin(skeleton, pose, pose, pose, pose, 0.0f) == false);
    assert(inertializer.IsActive() == false);

    PoseInertializerBoneDebugInfo debugInfo{};
    assert(inertializer.GetBoneDebugInfo(rootIndex, debugInfo) == false);
}
} // namespace

void RunPoseInertializerSelfTests()
{
    RunVelocityAwareTranslationTest();
    RunVelocityAwareRotationTest();
    RunQuaternionSignContinuityTest();
    RunInvalidVelocityDeltaTimeTest();
}

} // namespace Raven::tests
