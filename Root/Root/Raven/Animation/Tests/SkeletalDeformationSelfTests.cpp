// Raven/Animation/Tests/SkeletalDeformationSelfTests.cpp
#include "Raven/Animation/Tests/SkeletalDeformationSelfTests.h"

#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Animation/SkeletalMeshDeformer.h"
#include "Raven/Animation/Skinning.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven::tests
{
namespace
{
bool NearlyEqual(float a, float b, float tolerance = 1.0e-4f)
{
    return std::abs(a - b) <= tolerance;
}

bool NearlyEqual(const math::Vec3& a, const math::Vec3& b, float tolerance = 1.0e-4f)
{
    return NearlyEqual(a.x, b.x, tolerance)
        && NearlyEqual(a.y, b.y, tolerance)
        && NearlyEqual(a.z, b.z, tolerance);
}

SkinWeight MakeWeight(BoneIndex bone, float weight = 1.0f)
{
    SkinWeight result{};
    assert(result.AddInfluence(bone, weight));
    assert(result.Normalize());
    return result;
}

void RunBindPoseIdentityTest()
{
    Skeleton skeleton;

    Bone root{};
    root.Name = "Root";
    root.BindLocalTransform.Translation = { 2.0f, 0.0f, 0.0f };
    const BoneIndex rootIndex = skeleton.AddBone(root);
    assert(rootIndex != InvalidBoneIndex);

    Bone child{};
    child.Name = "Child";
    child.Parent = rootIndex;
    child.BindLocalTransform.Translation = { 0.0f, 1.0f, 0.0f };
    const BoneIndex childIndex = skeleton.AddBone(child);
    assert(childIndex != InvalidBoneIndex);

    SkeletonPose pose;
    pose.ResetToBindPose(skeleton);

    std::vector<math::Mat4> matrices;
    assert(BuildSkinningMatrices(skeleton, pose, matrices));

    // Bind Poseでは CurrentGlobal == BindGlobal なので、
    // CurrentGlobal * InverseBind はRoot/ChildともIdentityでなければなりません。
    assert(AreSkinningMatricesIdentity(matrices));
}

void RunExternalBindSpaceCorrectionTest()
{
    Skeleton skeleton;

    Bone root{};
    root.Name = "Root";
    root.BindLocalTransform.Translation = { 2.0f, 0.0f, 0.0f };

    // ========================================================================
    // glTFのRoot Joint外側Transformを模した最小ケース
    // ========================================================================
    // Raven Skeleton内のRoot BindGlobalはT(+2)ですが、glTF inverseBindには
    // Scene上のJoint WorldとMesh Worldの差が含まれるため、ここでは意図的にT(-7)を与えます。
    //
    //   BindSkeletonGlobal * InverseBind = T(+2) * T(-7) = T(-5)
    //
    // SkeletalMeshDeformerは全Boneに共通するこのBind空間差を検出し、逆行列T(+5)を
    // 左から補正します。その結果Bind Pose SkinningはIdentityへ戻る必要があります。
    const math::Mat4 externalInverseBind = math::Mat4::Translation({ -7.0f, 0.0f, 0.0f });
    const BoneIndex rootIndex = skeleton.AddBoneWithInverseBindMatrix(root, externalInverseBind);
    assert(rootIndex != InvalidBoneIndex);

    const std::vector<math::Vec3> bindPositions{
        { 3.0f, 0.0f, 0.0f }
    };
    const std::vector<SkinWeight> weights{
        MakeWeight(rootIndex)
    };
    SkinnedMeshData skinData(bindPositions, weights);

    std::vector<MeshVertex> vertices(1);
    vertices[0].Position = bindPositions[0];
    MeshGeometry geometry(std::move(vertices), {}, GeometryUsage::Dynamic, TopologyUsage::Fixed);

    SkeletalMeshDeformer deformer(skeleton, skinData);

    // Bind Poseでは補正込みSkinning MatrixがIdentityになり、頂点は元位置を維持します。
    assert(deformer.Deform(
        deformer.GetSkeleton(),
        deformer.GetPose(),
        deformer.GetSkinnedMeshData(),
        geometry));
    assert(NearlyEqual(geometry.GetVertices()[0].Position, bindPositions[0]));

    // RootをBind位置+1だけ移動すると、外側基準空間差に影響されず頂点も+1だけ移動します。
    BoneTransform moved = deformer.GetPose().GetLocalTransform(rootIndex);
    moved.Translation = { 3.0f, 0.0f, 0.0f };
    assert(deformer.GetPose().SetLocalTransform(rootIndex, moved));
    assert(deformer.GetPose().UpdateGlobalTransforms(deformer.GetSkeleton()));
    assert(deformer.Deform(
        deformer.GetSkeleton(),
        deformer.GetPose(),
        deformer.GetSkinnedMeshData(),
        geometry));
    assert(NearlyEqual(geometry.GetVertices()[0].Position, math::Vec3{ 4.0f, 0.0f, 0.0f }));
}

void RunOneBoneManualPoseTest()
{
    Skeleton skeleton;
    Bone root{};
    root.Name = "Root";
    const BoneIndex rootIndex = skeleton.AddBone(root);
    assert(rootIndex != InvalidBoneIndex);

    const std::vector<math::Vec3> bindPositions{
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f }
    };
    const std::vector<SkinWeight> weights{
        MakeWeight(rootIndex),
        MakeWeight(rootIndex)
    };
    SkinnedMeshData skinData(bindPositions, weights);

    std::vector<MeshVertex> vertices(2);
    vertices[0].Position = bindPositions[0];
    vertices[1].Position = bindPositions[1];
    MeshGeometry geometry(std::move(vertices), {}, GeometryUsage::Dynamic, TopologyUsage::Fixed);

    SkeletonPose pose;
    pose.ResetToBindPose(skeleton);

    SkeletalMeshDeformer deformer(skeleton, skinData);

    // まずBind Poseを通し、頂点が全く動かないことを確認します。
    assert(deformer.Deform(skeleton, pose, skinData, geometry));
    assert(NearlyEqual(geometry.GetVertices()[0].Position, bindPositions[0]));
    assert(NearlyEqual(geometry.GetVertices()[1].Position, bindPositions[1]));

    // Rootを+Yへ2移動すると、100% Root Weightの全頂点が同量だけ移動します。
    BoneTransform moved = pose.GetLocalTransform(rootIndex);
    moved.Translation = { 0.0f, 2.0f, 0.0f };
    assert(pose.SetLocalTransform(rootIndex, moved));
    assert(pose.UpdateGlobalTransforms(skeleton));
    assert(deformer.Deform(skeleton, pose, skinData, geometry));

    assert(NearlyEqual(geometry.GetVertices()[0].Position, math::Vec3{ 0.0f, 2.0f, 0.0f }));
    assert(NearlyEqual(geometry.GetVertices()[1].Position, math::Vec3{ 1.0f, 2.0f, 0.0f }));
}

void RunTwoBoneBlendTest()
{
    Skeleton skeleton;

    Bone root{};
    root.Name = "Root";
    const BoneIndex rootIndex = skeleton.AddBone(root);
    assert(rootIndex != InvalidBoneIndex);

    Bone child{};
    child.Name = "Child";
    child.Parent = rootIndex;
    child.BindLocalTransform.Translation = { 1.0f, 0.0f, 0.0f };
    const BoneIndex childIndex = skeleton.AddBone(child);
    assert(childIndex != InvalidBoneIndex);

    // x=0はRoot固定、x=1は関節位置で50/50、x=2はChild固定という最小Stripです。
    const std::vector<math::Vec3> bindPositions{
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 2.0f, 0.0f, 0.0f }
    };

    SkinWeight middle{};
    assert(middle.AddInfluence(rootIndex, 0.5f));
    assert(middle.AddInfluence(childIndex, 0.5f));
    assert(middle.Normalize());

    const std::vector<SkinWeight> weights{
        MakeWeight(rootIndex),
        middle,
        MakeWeight(childIndex)
    };
    SkinnedMeshData skinData(bindPositions, weights);

    std::vector<MeshVertex> vertices(3);
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i].Position = bindPositions[i];
    }
    MeshGeometry geometry(std::move(vertices), {}, GeometryUsage::Dynamic, TopologyUsage::Fixed);

    SkeletonPose pose;
    pose.ResetToBindPose(skeleton);

    // ChildのBind位置(x=1)をPivotとしてZ軸へ90度回転します。
    // Child 100%の(x=2,0)は(x=1,1)へ移動するはずです。
    BoneTransform childPose = pose.GetLocalTransform(childIndex);
    childPose.Rotation = math::Quat::FromAxisAngle({ 0.0f, 0.0f, 1.0f }, 1.57079632679f);
    assert(pose.SetLocalTransform(childIndex, childPose));
    assert(pose.UpdateGlobalTransforms(skeleton));

    SkeletalMeshDeformer deformer(skeleton, skinData);
    assert(deformer.Deform(skeleton, pose, skinData, geometry));

    const auto& result = geometry.GetVertices();
    assert(NearlyEqual(result[0].Position, math::Vec3{ 0.0f, 0.0f, 0.0f }));

    // 関節そのもの(x=1)はChild回転のPivot上なのでRoot/Childどちらで変換しても同位置です。
    assert(NearlyEqual(result[1].Position, math::Vec3{ 1.0f, 0.0f, 0.0f }));
    assert(NearlyEqual(result[2].Position, math::Vec3{ 1.0f, 1.0f, 0.0f }));
}
} // namespace

void RunSkeletalDeformationSelfTests()
{
    RunBindPoseIdentityTest();
    RunExternalBindSpaceCorrectionTest();
    RunOneBoneManualPoseTest();
    RunTwoBoneBlendTest();
}

} // namespace Raven::tests
