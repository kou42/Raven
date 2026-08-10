// Raven/Animation/SkeletalMeshDeformer.h
#pragma once

#include <vector>

#include "Raven/Animation/Skeleton.h"
#include "Raven/Animation/SkeletonPose.h"
#include "Raven/Animation/SkinnedMeshData.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven
{

class MeshGeometry;

// ============================================================================
// SkeletalMeshDeformer
// ============================================================================
// CPU上でLinear Blend Skinning(LBS)を行い、MeshGeometryのPositionを更新します。
//
// このクラスはAnimationClipの再生責務を持ちません。
// SkeletonPoseへ現在姿勢が既に書き込まれていることを前提に、
//   SkeletonPose -> Skinning Matrix -> Vertex Position -> MeshGeometry
// という「変形」だけを担当します。
//
// CPU Skinningを先に完成させることで、Bone階層・Inverse Bind・Weight・行列規約を
// GPU Shaderから切り離して検証できます。GPU Skinningへ移行するときも、ここで確立した
// Skinning Matrixの意味をそのままShaderへ持ち込めます。
class SkeletalMeshDeformer
{
public:
    // 現在PoseからSkinning Matrixを再構築し、全頂点を変形してGeometryへ反映します。
    // GeometryはDynamicかつ、SkinnedMeshDataと同じ頂点数である必要があります。
    bool Deform(
        const Skeleton& skeleton,
        const SkeletonPose& pose,
        const SkinnedMeshData& skinnedMeshData,
        MeshGeometry& geometry);

    // Skinning Matrixを既に構築済みの場合の低レベル経路です。
    // Testや、複数Meshで同一Skeleton Poseを共有する場合にMatrix計算を再利用できます。
    bool DeformWithMatrices(
        const Skeleton& skeleton,
        const SkinnedMeshData& skinnedMeshData,
        const std::vector<math::Mat4>& skinningMatrices,
        MeshGeometry& geometry);

    const std::vector<math::Mat4>& GetSkinningMatrices() const
    {
        return m_SkinningMatrices;
    }

private:
    std::vector<math::Mat4> m_SkinningMatrices;
};

} // namespace Raven
