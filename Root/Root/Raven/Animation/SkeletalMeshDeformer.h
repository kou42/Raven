// Raven/Animation/SkeletalMeshDeformer.h
#pragma once

#include <utility>
#include <vector>

#include "Raven/Animation/Skeleton.h"
#include "Raven/Animation/SkeletonPose.h"
#include "Raven/Animation/SkinnedMeshData.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformer.h"

namespace Raven
{

class Mesh;
class MeshGeometry;

// ============================================================================
// SkeletalMeshDeformer
// ============================================================================
// CPU Linear Blend Skinningを既存MeshDeformationSystemへ接続するDeformerです。
//
// Skeleton       : Bone階層 / Bind Pose / Inverse Bindという共有可能な定義データ
// SkeletonPose   : このInstanceだけが持つ現在Pose
// SkinnedMeshData: MeshのBind Position / Bone Weight
//
// を所有し、MeshDeformer::Update()では「現在PoseをMeshへ反映する」ことだけを行います。
// AnimationClip / Animatorは後からSkeletonPoseを書き換える側として追加できます。
class SkeletalMeshDeformer final : public MeshDeformer
{
public:
    SkeletalMeshDeformer(Skeleton skeleton, SkinnedMeshData skinnedMeshData)
        : m_Skeleton(std::move(skeleton)),
          m_SkinnedMeshData(std::move(skinnedMeshData))
    {
        // 最初は必ずBind Poseから開始します。
        // Animationが未接続でもUpdate()を呼べば元形状をそのまま再現できます。
        m_Pose.ResetToBindPose(m_Skeleton);
    }

    // MeshDeformationSystemから呼ばれる通常経路です。
    // deltaTimeは現段階では使いません。時間管理はAnimatorの責務として後から分離します。
    void Update(Mesh& mesh, float deltaTime) override;

    // 手動Pose Test / 将来Animatorが現在Poseを書き換えるための入口です。
    SkeletonPose& GetPose() { return m_Pose; }
    const SkeletonPose& GetPose() const { return m_Pose; }
    const Skeleton& GetSkeleton() const { return m_Skeleton; }
    const SkinnedMeshData& GetSkinnedMeshData() const { return m_SkinnedMeshData; }

    // 現在PoseからSkinning Matrixを再構築し、全頂点を変形してGeometryへ反映します。
    bool Deform(
        const Skeleton& skeleton,
        const SkeletonPose& pose,
        const SkinnedMeshData& skinnedMeshData,
        MeshGeometry& geometry);

    // TestなどでSkinning Matrixを直接与える低レベル経路です。
    bool DeformWithMatrices(
        const Skeleton& skeleton,
        const SkinnedMeshData& skinnedMeshData,
        const std::vector<math::Mat4>& skinningMatrices,
        MeshGeometry& geometry);

    const std::vector<math::Mat4>& GetSkinningMatrices() const { return m_SkinningMatrices; }

private:
    Skeleton m_Skeleton;
    SkeletonPose m_Pose;
    SkinnedMeshData m_SkinnedMeshData;
    std::vector<math::Mat4> m_SkinningMatrices;
};

} // namespace Raven
