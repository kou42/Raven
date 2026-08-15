// Raven/Animation/SkeletalMeshDeformer.h
#pragma once

#include <string>
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
    SkeletalMeshDeformer(Skeleton skeleton, SkinnedMeshData skinnedMeshData);

    // MeshDeformationSystemから呼ばれる通常経路です。
    // deltaTimeは現段階では使いません。時間管理はAnimatorの責務として後から分離します。
    void Update(Mesh& mesh, float deltaTime) override;

    // 手動Pose Test / 将来Animatorが現在Poseを書き換えるための入口です。
    SkeletonPose& GetPose() { return m_Pose; }
    const SkeletonPose& GetPose() const { return m_Pose; }
    const Skeleton& GetSkeleton() const { return m_Skeleton; }
    const SkinnedMeshData& GetSkinnedMeshData() const { return m_SkinnedMeshData; }
    const math::Mat4& GetSkeletonParentToMeshTransform() const
    {
        return m_SkeletonParentToMeshTransform;
    }
    const std::string& GetBindSpaceCorrectionError() const
    {
        return m_BindSpaceCorrectionError;
    }

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

    // ========================================================================
    // Skeleton Parent Space -> Mesh Local Space
    // ========================================================================
    // RavenのSkeleton GlobalはRoot Boneからの相対空間です。一方glTFのinverseBindMatricesは
    // Mesh Bind SpaceとScene上のJoint World Spaceの関係を含められます。
    //
    // Bind Poseでは全Boneについて
    //
    //   BindSkeletonGlobal * InverseBind
    //
    // が同一の基準空間差になります。その共通行列の逆行列を一度だけ求め、Skinning時に
    // 左から掛けることでMesh Local Spaceへ戻します。手作りSkeletonでは共通行列がIdentityに
    // なるため、従来挙動をそのまま維持します。
    math::Mat4 m_SkeletonParentToMeshTransform = math::Mat4::Identity();
    bool m_BindSpaceCorrectionValid = false;

    // 補正行列を構築できなかった場合の詳細理由です。
    // Human.glbのような実Assetで「どのBoneから空間差が崩れたか」を上位で診断できるよう、
    // 単なるboolだけで失敗理由を失わないようにします。
    std::string m_BindSpaceCorrectionError;

    std::vector<math::Mat4> m_SkinningMatrices;
};

} // namespace Raven
