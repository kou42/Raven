// Raven/Physics/Tests/RagdollAnimationVelocitySelfTests.cpp
#include <cassert>
#include <cmath>

#include "Raven/Animation/Skeleton.h"
#include "Raven/Animation/SkeletonPose.h"
#include "Raven/Physics/Ragdoll/RagdollRuntime.h"

namespace Raven::ph::tests
{
namespace
{

bool NearlyEqual(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}

Skeleton CreateSingleBoneSkeleton()
{
    Skeleton skeleton;

    Bone root{};
    root.Name = "Root";
    root.Parent = InvalidBoneIndex;
    root.BindLocalTransform = BoneTransform{};
    skeleton.AddBone(root);
    return skeleton;
}

RagdollDefinition CreateSingleBodyDefinition()
{
    RagdollDefinition definition{};

    RagdollBodyDefinition body{};
    body.BoneName = "Root";
    body.Mass = 1.0f;
    body.Radius = 0.25f;
    body.HalfLength = 0.5f;
    definition.Bodies.push_back(body);
    return definition;
}

} // namespace

void RunRagdollAnimationVelocitySelfTests()
{
    Skeleton skeleton = CreateSingleBoneSkeleton();
    SkeletonPose pose;
    pose.ResetToBindPose(skeleton);

    RagdollRuntime ragdoll;
    assert(ragdoll.Build(skeleton, CreateSingleBodyDefinition()));

    // ========================================================================
    // First Sample
    // ========================================================================
    // 比較元Frameが無い最初のSampleでは、現在Poseは取り込むものの速度は0であるべきです。
    assert(ragdoll.SampleAnimationPose(pose, 1.0f / 60.0f));
    const RagdollBodyState* first = ragdoll.FindBody("Root");
    assert(first != nullptr);
    assert(first->LinearVelocity.LengthSq() <= 1.0e-12f);
    assert(first->AngularVelocity.LengthSq() <= 1.0e-12f);

    // 0.5秒でXへ1m移動し、Z軸へ90度回転させます。
    // 期待値は LinearVelocity=(2,0,0), AngularVelocity=(0,0,PI) です。
    BoneTransform moved = pose.GetLocalTransform(0u);
    moved.Translation = { 1.0f, 0.0f, 0.0f };
    moved.Rotation = math::Quat::FromAxisAngle(
        { 0.0f, 0.0f, 1.0f },
        1.57079632679f);
    assert(pose.SetLocalTransform(0u, moved));

    assert(ragdoll.SampleAnimationPose(pose, 0.5f));
    const RagdollBodyState* moving = ragdoll.FindBody("Root");
    assert(moving != nullptr);
    assert(NearlyEqual(moving->LinearVelocity.x, 2.0f));
    assert(NearlyEqual(moving->LinearVelocity.y, 0.0f));
    assert(NearlyEqual(moving->LinearVelocity.z, 0.0f));
    assert(NearlyEqual(moving->AngularVelocity.x, 0.0f));
    assert(NearlyEqual(moving->AngularVelocity.y, 0.0f));
    assert(NearlyEqual(moving->AngularVelocity.z, 3.14159265359f, 1.0e-3f));

    // ========================================================================
    // Quaternion sign equivalence
    // ========================================================================
    // qと-qは同じ姿勢です。Animation側でQuaternion符号だけが反転しても、2PI相当の巨大な
    // AngularVelocityを生成してはいけません。
    BoneTransform samePose = moved;
    samePose.Rotation.x = -samePose.Rotation.x;
    samePose.Rotation.y = -samePose.Rotation.y;
    samePose.Rotation.z = -samePose.Rotation.z;
    samePose.Rotation.w = -samePose.Rotation.w;
    assert(pose.SetLocalTransform(0u, samePose));

    assert(ragdoll.SampleAnimationPose(pose, 1.0f / 60.0f));
    const RagdollBodyState* signFlipped = ragdoll.FindBody("Root");
    assert(signFlipped != nullptr);
    assert(signFlipped->LinearVelocity.LengthSq() <= 1.0e-10f);
    assert(signFlipped->AngularVelocity.LengthSq() <= 1.0e-8f);

    // Teleport / Time Seek相当の不連続点では履歴をResetします。
    // 次Sampleは新しい基準Poseとなるため、位置が大きく飛んでも速度0から再開します。
    ragdoll.ResetAnimationVelocityHistory();
    BoneTransform teleported = samePose;
    teleported.Translation = { 100.0f, 20.0f, -30.0f };
    assert(pose.SetLocalTransform(0u, teleported));
    assert(ragdoll.SampleAnimationPose(pose, 1.0f / 60.0f));

    const RagdollBodyState* reset = ragdoll.FindBody("Root");
    assert(reset != nullptr);
    assert(reset->LinearVelocity.LengthSq() <= 1.0e-12f);
    assert(reset->AngularVelocity.LengthSq() <= 1.0e-12f);
}

} // namespace Raven::ph::tests
