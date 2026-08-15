// Raven/Animation/SkeletalMeshDeformer.cpp
#include "Raven/Animation/SkeletalMeshDeformer.h"

#include <cstddef>

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
} // namespace

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
    // glTFではMesh NodeとSkeleton Rootが別Node階層に置かれることがあり、
    // SkeletonPose::GlobalはRoot Joint外側のScene Node Transformを含みません。
    // その場合はRuntime構築時に求めた基準空間補正を左から掛け、
    // 最終的なSkinning MatrixをMesh Local Spaceへ戻します。
    //
    //   MeshLocalSkin = SkeletonParentToMesh
    //                 * SkeletonGlobal
    //                 * InverseBind
    //
    // 手作りSkeletonなど基準空間差がない場合、この補正はIdentityなので既存挙動と同一です。
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
