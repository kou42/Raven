// Raven/Gltf/SkinnedMeshRuntime.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Animation/Bone.h"
#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Gltf/SkinnedMeshImporter.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven
{

class Mesh;
class MeshDeformationInstance;
class SkeletalMeshDeformer;

namespace Gltf
{

// ============================================================================
// RuntimeSkinnedPrimitive
// ============================================================================
// ImportedSkinnedPrimitiveを実際のRaven Runtimeへ接続した1描画Primitiveです。
//
// GeometryはMeshが所有し、SkeletalMeshDeformerはMeshDeformationInstance内で所有します。
// この構造体はScene側が描画用Meshと変形Instanceを同じPrimitiveとして扱えるよう、
// それぞれへのRefとglTF由来の識別情報だけを保持します。
struct RuntimeSkinnedPrimitive
{
    std::string MeshName;

    std::size_t NodeIndex = InvalidGltfIndex;
    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t PrimitiveIndex = InvalidGltfIndex;
    std::size_t MaterialIndex = InvalidGltfIndex;
    std::size_t SkinIndex = InvalidGltfIndex;

    math::Mat4 WorldTransform = math::Mat4::Identity();

    Ref<Mesh> MeshInstance;
    Ref<MeshDeformationInstance> DeformationInstance;
};

// ============================================================================
// SkinnedMeshRuntimeAsset
// ============================================================================
// glTF Import結果を既存の Mesh + MeshDeformationInstance + SkeletalMeshDeformer へ接続します。
//
// 重要:
// 1つのglTF SkinをBody / Clothesなど複数Primitiveが共有する場合でも、現在の
// SkeletalMeshDeformerはPrimitiveごとにSkeletonPoseを所有します。
// そのためBone操作はこのAssetを入口にし、同じSkinIndexを使う全Primitiveへ同期して
// 適用します。将来AnimationClip / Animatorを接続するときも同じ同期境界を利用できます。
class SkinnedMeshRuntimeAsset
{
public:
    // Import済みAssetからRuntime Instanceを構築します。
    // 各PrimitiveはDynamic MeshとSkeletalMeshDeformerを1組ずつ持ちます。
    bool Build(
        const ImportedSkinnedAsset& importedAsset,
        std::string* errorMessage = nullptr);

    // GLB読込からRuntime構築までを一括で行う利便APIです。
    bool LoadFromGlb(
        const std::string& filePath,
        std::string* errorMessage = nullptr);

    // 指定Skinを参照する全PrimitiveのPoseをBind Poseへ戻します。
    bool ResetSkinToBindPose(
        std::size_t skinIndex,
        std::string* errorMessage = nullptr);

    // Bone名でLocal Transformを設定します。
    // 同じSkinを使う全Primitiveへ同じ値を適用し、Body/ClothesなどのPoseずれを防ぎます。
    bool SetBoneLocalTransform(
        std::size_t skinIndex,
        const std::string& boneName,
        const BoneTransform& transform,
        std::string* errorMessage = nullptr);

    // 現在Poseを全Primitiveへ反映します。
    // MeshDeformationInstance::Update()を通すため、CPU頂点変形からGPU同期まで既存経路を使用します。
    bool Update(float deltaTime, std::string* errorMessage = nullptr);

    const std::vector<RuntimeSkinnedPrimitive>& GetPrimitives() const
    {
        return m_Primitives;
    }

private:
    SkeletalMeshDeformer* GetSkeletalDeformer(
        const RuntimeSkinnedPrimitive& primitive,
        std::string* errorMessage) const;

private:
    std::vector<RuntimeSkinnedPrimitive> m_Primitives;
};

} // namespace Gltf
} // namespace Raven
