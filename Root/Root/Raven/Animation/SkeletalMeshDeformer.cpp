// Raven/Animation/SkeletalMeshDeformer.cpp
#include "Raven/Animation/SkeletalMeshDeformer.h"

#include <cstddef>

#include "Raven/Animation/SkinWeight.h"
#include "Raven/Animation/Skinning.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{

namespace
{

// Positionは点なので同次座標w=1としてSkinning Matrixを作用させます。
// 現在のBone TransformはAffine Transformなので通常result.wは1ですが、
// Mat4の意味を曖昧にしないためVec4経由で明示的に変換します。
math::Vec3 TransformPosition(const math::Mat4& matrix, const math::Vec3& position)
{
    const math::Vec4 transformed = matrix * math::Vec4{ position.x, position.y, position.z, 1.0f };
    return { transformed.x, transformed.y, transformed.z };
}

} // namespace

bool SkeletalMeshDeformer::Deform(
    const Skeleton& skeleton,
    const SkeletonPose& pose,
    const SkinnedMeshData& skinnedMeshData,
    MeshGeometry& geometry)
{
    // Skinning MatrixはCurrentGlobal * InverseBindです。
    // Poseだけが毎Frame変化するため、Bind PositionやWeightは変更せずMatrixだけを再構築します。
    if (!BuildSkinningMatrices(skeleton, pose, m_SkinningMatrices))
        return false;

    return DeformWithMatrices(skeleton, skinnedMeshData, m_SkinningMatrices, geometry);
}

bool SkeletalMeshDeformer::DeformWithMatrices(
    const Skeleton& skeleton,
    const SkinnedMeshData& skinnedMeshData,
    const std::vector<math::Mat4>& skinningMatrices,
    MeshGeometry& geometry)
{
    // CPU Skinning入力の契約を変形前にまとめて検証します。
    // 途中まで頂点を書き換えてから失敗するとGeometryが半端な状態になるため、
    // すべてのValidationを済ませてから出力頂点列を作ります。
    if (!skinnedMeshData.Validate(skeleton))
        return false;

    if (skinningMatrices.size() != skeleton.GetBoneCount())
        return false;

    if (geometry.GetGeometryUsage() != GeometryUsage::Dynamic)
        return false;

    const std::vector<MeshVertex>& sourceVertices = geometry.GetVertices();
    if (sourceVertices.size() != skinnedMeshData.GetVertexCount())
        return false;

    // Color / TexCoordなどSkinning対象外の属性は現在Geometryに入っている値を保持し、
    // PositionだけをBind Positionから毎回再計算します。
    // 「前Frameの変形済みPosition」を入力にしないことが重要です。
    // それを入力にすると変形誤差がFrameごとに累積し、Meshが徐々に崩れてしまいます。
    std::vector<MeshVertex> deformedVertices = sourceVertices;

    const std::vector<math::Vec3>& bindPositions = skinnedMeshData.GetBindPositions();
    const std::vector<SkinWeight>& skinWeights = skinnedMeshData.GetSkinWeights();

    for (std::size_t vertexIndex = 0; vertexIndex < bindPositions.size(); ++vertexIndex)
    {
        const math::Vec3& bindPosition = bindPositions[vertexIndex];
        const SkinWeight& skinWeight = skinWeights[vertexIndex];

        math::Vec3 deformedPosition{ 0.0f, 0.0f, 0.0f };

        // Linear Blend Skinning:
        //   p' = Sum(weight_i * (CurrentGlobal_i * InverseBind_i * p_bind))
        //
        // MaxBoneInfluencesは4なので、GPU Skinningへ移行した場合も一般的な
        // ivec4 BoneIndices + vec4 Weights のVertex Attributeへ素直に対応できます。
        for (std::size_t influenceIndex = 0; influenceIndex < MaxBoneInfluences; ++influenceIndex)
        {
            const float weight = skinWeight.Weights[influenceIndex];
            if (weight <= 0.0f)
                continue;

            const BoneIndex boneIndex = skinWeight.BoneIndices[influenceIndex];
            if (!skeleton.IsValidBoneIndex(boneIndex))
                return false;

            const math::Vec3 transformedPosition =
                TransformPosition(skinningMatrices[static_cast<std::size_t>(boneIndex)], bindPosition);

            deformedPosition += transformedPosition * weight;
        }

        deformedVertices[vertexIndex].Position = deformedPosition;
    }

    // SetVertices()がRevisionを進め、次のMesh::SyncGeometry()で既存VBOへUploadされます。
    // SkeletalMeshDeformer自身はRenderer/GPU APIを知らないまま、CPU Geometryだけを更新します。
    return geometry.SetVertices(std::move(deformedVertices));
}

} // namespace Raven
