// Raven/Animation/SkeletalMeshDeformer.cpp
#include "Raven/Animation/SkeletalMeshDeformer.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "Raven/Animation/SkinWeight.h"
#include "Raven/Animation/Skinning.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{
namespace
{

math::Vec3 TransformPosition(const math::Mat4& matrix, const math::Vec3& position)
{
    const math::Vec4 transformed = matrix * math::Vec4{ position.x, position.y, position.z, 1.0f };
    return { transformed.x, transformed.y, transformed.z };
}

float MaxAbsDifference(const math::Mat4& a, const math::Mat4& b)
{
    float maxDifference = 0.0f;

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const float difference = std::fabs(a[row][column] - b[row][column]);
            if (difference > maxDifference)
            {
                maxDifference = difference;
            }
        }
    }

    return maxDifference;
}

bool TryInvertAffineTransform(
    const math::Mat4& matrix,
    math::Mat4& outInverse,
    std::string* errorMessage)
{
    constexpr float AffineTolerance = 1.0e-5f;
    constexpr float DeterminantTolerance = 1.0e-8f;

    // Skinningの基準空間補正はScene Node由来のAffine Transformだけを対象にします。
    // Perspective成分が混ざった行列をSkinningへ通すことは座標空間の契約違反なので拒否します。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Bind Space差分行列がAffine Transformではありません";
        }
        return false;
    }

    const float a00 = matrix[0][0];
    const float a01 = matrix[0][1];
    const float a02 = matrix[0][2];
    const float a10 = matrix[1][0];
    const float a11 = matrix[1][1];
    const float a12 = matrix[1][2];
    const float a20 = matrix[2][0];
    const float a21 = matrix[2][1];
    const float a22 = matrix[2][2];

    const float determinant =
        a00 * (a11 * a22 - a12 * a21)
        - a01 * (a10 * a22 - a12 * a20)
        + a02 * (a10 * a21 - a11 * a20);

    if (std::isfinite(determinant) == false
        || std::fabs(determinant) <= DeterminantTolerance)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage =
                "Bind Space差分行列を逆行列化できません。determinant="
                + std::to_string(determinant);
        }
        return false;
    }

    const float inverseDeterminant = 1.0f / determinant;

    const float i00 = (a11 * a22 - a12 * a21) * inverseDeterminant;
    const float i01 = (a02 * a21 - a01 * a22) * inverseDeterminant;
    const float i02 = (a01 * a12 - a02 * a11) * inverseDeterminant;
    const float i10 = (a12 * a20 - a10 * a22) * inverseDeterminant;
    const float i11 = (a00 * a22 - a02 * a20) * inverseDeterminant;
    const float i12 = (a02 * a10 - a00 * a12) * inverseDeterminant;
    const float i20 = (a10 * a21 - a11 * a20) * inverseDeterminant;
    const float i21 = (a01 * a20 - a00 * a21) * inverseDeterminant;
    const float i22 = (a00 * a11 - a01 * a10) * inverseDeterminant;

    const float tx = matrix[0][3];
    const float ty = matrix[1][3];
    const float tz = matrix[2][3];

    // Affine Transform M=[A t; 0 1] の逆行列は [A^-1 -A^-1*t; 0 1] です。
    outInverse = math::Mat4{
        i00, i01, i02, -(i00 * tx + i01 * ty + i02 * tz),
        i10, i11, i12, -(i10 * tx + i11 * ty + i12 * tz),
        i20, i21, i22, -(i20 * tx + i21 * ty + i22 * tz),
        0.0f, 0.0f, 0.0f, 1.0f
    };

    return true;
}

bool TryBuildBindSpaceCorrection(
    const Skeleton& skeleton,
    const SkeletonPose& bindPose,
    math::Mat4& outCorrection,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (skeleton.GetBoneCount() == 0u)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "SkeletonのBone数が0です";
        }
        return false;
    }

    if (bindPose.GetBoneCount() != skeleton.GetBoneCount())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Bind PoseとSkeletonのBone数が一致しません";
        }
        return false;
    }

    // ========================================================================
    // Bind Space correction derivation
    // ========================================================================
    // Raven SkeletonのBindGlobalはRoot Bone外側のScene Node Transformを含みません。
    // glTFのinverseBindMatricesはMesh Bind Space -> Joint Space変換なので、Bind Poseでは
    // 各Boneについて次の積が同一の「Skeleton Parent Space -> Mesh Bind Space差」になります。
    //
    //   B = BindSkeletonGlobal * InverseBind
    //
    // Human.glbのようにArmature NodeがRoot Joint外側にある場合、BはIdentityではありません。
    // しかし全Boneで同じBになるため、その逆行列をSkinning Matrix左側へ掛ければ
    // Mesh Local Spaceへ正しく戻せます。
    const Bone& firstBone = skeleton.GetBone(0u);
    const math::Mat4 bindSpaceDifference =
        bindPose.GetGlobalTransform(0u) * firstBone.InverseBindMatrix;

    constexpr float BindSpaceTolerance = 1.0e-4f;

    for (BoneIndex boneIndex = 1u;
         boneIndex < static_cast<BoneIndex>(skeleton.GetBoneCount());
         ++boneIndex)
    {
        const Bone& bone = skeleton.GetBone(boneIndex);
        const math::Mat4 currentDifference =
            bindPose.GetGlobalTransform(boneIndex) * bone.InverseBindMatrix;

        const float maxDifference = MaxAbsDifference(currentDifference, bindSpaceDifference);
        if (maxDifference > BindSpaceTolerance)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage =
                    "BoneごとのBind Space差分が一致しません。bone="
                    + bone.Name
                    + ", index=" + std::to_string(boneIndex)
                    + ", maxDifference=" + std::to_string(maxDifference)
                    + ", tolerance=" + std::to_string(BindSpaceTolerance);
            }
            return false;
        }
    }

    return TryInvertAffineTransform(bindSpaceDifference, outCorrection, errorMessage);
}

} // namespace

SkeletalMeshDeformer::SkeletalMeshDeformer(
    Skeleton skeleton,
    SkinnedMeshData skinnedMeshData)
    : m_Skeleton(std::move(skeleton)),
      m_SkinnedMeshData(std::move(skinnedMeshData))
{
    // 最初は必ずBind Poseから開始します。
    // Animationが未接続でもUpdate()を呼べば元形状をそのまま再現できます。
    m_Pose.ResetToBindPose(m_Skeleton);

    // glTF由来SkeletonではRoot Joint外側のScene TransformをSkeletonPoseへ含めません。
    // 既存のBind PoseとinverseBindMatricesからその空間差を復元し、以後のSkinningで再利用します。
    m_BindSpaceCorrectionValid = TryBuildBindSpaceCorrection(
        m_Skeleton,
        m_Pose,
        m_SkeletonParentToMeshTransform,
        &m_BindSpaceCorrectionError);

    if (m_BindSpaceCorrectionValid == false)
    {
        std::cerr
            << "[SkeletalMeshDeformer] Bind Space補正の初期化に失敗しました: "
            << m_BindSpaceCorrectionError << '\n';
    }
}

void SkeletalMeshDeformer::Update(Mesh& mesh, float deltaTime)
{
    static_cast<void>(deltaTime);

    const Ref<MeshGeometry>& geometry = mesh.GetGeometry();
    if (geometry == nullptr)
    {
        return;
    }

    // Animation再生はまだ行わず、外部から設定済みのSkeletonPoseをそのままSkinningします。
    // これにより1/2 Boneの手動Pose TestをAnimationClipより先に検証できます。
    if (Deform(m_Skeleton, m_Pose, m_SkinnedMeshData, *geometry) == false)
    {
        return;
    }

    // MeshGeometry::SetVertices()でRevisionが進んだため、既存のDeformation経路と同様に
    // ここでGPU VBOへ同期します。Scene / ECS側はSkeletal固有処理を知りません。
    mesh.SyncGeometry();
}

bool SkeletalMeshDeformer::Deform(
    const Skeleton& skeleton,
    const SkeletonPose& pose,
    const SkinnedMeshData& skinnedMeshData,
    MeshGeometry& geometry)
{
    if (m_BindSpaceCorrectionValid == false)
    {
        return false;
    }

    if (BuildSkinningMatrices(skeleton, pose, m_SkinningMatrices) == false)
    {
        return false;
    }

    // ========================================================================
    // Skeleton Parent Space -> Mesh Local Space
    // ========================================================================
    // BuildSkinningMatrices()はRaven Skeleton自身の数学だけを担当し、
    //
    //   SkeletonGlobal * InverseBind
    //
    // を生成します。
    //
    // Root Joint外側にTransformがあるglTFでは、Bind Poseから復元した基準空間補正を
    // 左から掛けてMesh Local Spaceへ戻します。
    //
    //   MeshLocalSkin = SkeletonParentToMesh
    //                 * SkeletonGlobal
    //                 * InverseBind
    //
    // 手作りSkeletonでは補正がIdentityになるため既存挙動と同一です。
    for (math::Mat4& skinningMatrix : m_SkinningMatrices)
    {
        skinningMatrix = m_SkeletonParentToMeshTransform * skinningMatrix;
    }

    return DeformWithMatrices(skeleton, skinnedMeshData, m_SkinningMatrices, geometry);
}

bool SkeletalMeshDeformer::DeformWithMatrices(
    const Skeleton& skeleton,
    const SkinnedMeshData& skinnedMeshData,
    const std::vector<math::Mat4>& skinningMatrices,
    MeshGeometry& geometry)
{
    if (skinnedMeshData.Validate(skeleton) == false)
    {
        return false;
    }
    if (skinningMatrices.size() != skeleton.GetBoneCount())
    {
        return false;
    }
    if (geometry.GetGeometryUsage() != GeometryUsage::Dynamic)
    {
        return false;
    }

    const std::vector<MeshVertex>& sourceVertices = geometry.GetVertices();
    if (sourceVertices.size() != skinnedMeshData.GetVertexCount())
    {
        return false;
    }

    // Positionだけを毎回Bind Positionから再計算します。
    // 前Frameの変形結果を入力にすると変形が累積するため、sourceVerticesはColor/UV保持にだけ使います。
    std::vector<MeshVertex> deformedVertices = sourceVertices;
    const auto& bindPositions = skinnedMeshData.GetBindPositions();
    const auto& skinWeights = skinnedMeshData.GetSkinWeights();

    for (std::size_t vertexIndex = 0; vertexIndex < bindPositions.size(); ++vertexIndex)
    {
        const math::Vec3& bindPosition = bindPositions[vertexIndex];
        const SkinWeight& skinWeight = skinWeights[vertexIndex];
        math::Vec3 deformedPosition{};

        // Linear Blend Skinning:
        // p' = Sum(w_i * (M_skin_i * p_bind))
        for (std::size_t influenceIndex = 0; influenceIndex < MaxBoneInfluences; ++influenceIndex)
        {
            const float weight = skinWeight.Weights[influenceIndex];
            if (weight <= 0.0f)
            {
                continue;
            }

            const BoneIndex boneIndex = skinWeight.BoneIndices[influenceIndex];
            if (skeleton.IsValidBoneIndex(boneIndex) == false)
            {
                return false;
            }

            deformedPosition += TransformPosition(
                skinningMatrices[static_cast<std::size_t>(boneIndex)], bindPosition) * weight;
        }

        deformedVertices[vertexIndex].Position = deformedPosition;
    }

    return geometry.SetVertices(std::move(deformedVertices));
}

} // namespace Raven
