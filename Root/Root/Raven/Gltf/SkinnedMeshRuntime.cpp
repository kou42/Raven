// Raven/Gltf/SkinnedMeshRuntime.cpp
#include "Raven/Gltf/SkinnedMeshRuntime.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Animation/SkeletalMeshDeformer.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

namespace Raven
{
namespace Gltf
{
namespace
{

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

bool NearlyEqual(float a, float b, float tolerance = 1.0e-4f)
{
    const float difference = a - b;
    return difference >= -tolerance && difference <= tolerance;
}

bool NearlyEqual(const math::Vec3& a, const math::Vec3& b, float tolerance = 1.0e-4f)
{
    return NearlyEqual(a.x, b.x, tolerance)
        && NearlyEqual(a.y, b.y, tolerance)
        && NearlyEqual(a.z, b.z, tolerance);
}

bool VerifyBindPoseGeometry(
    const ImportedSkinnedPrimitive& primitive,
    const ImportedSkin& skin,
    SkeletalMeshDeformer& deformer,
    std::string* errorMessage)
{
    if (primitive.Geometry == nullptr)
    {
        return SetError(errorMessage, "Skinned PrimitiveのGeometryがnullptrです");
    }

    const std::vector<math::Vec3>& bindPositions = primitive.SkinningData.GetBindPositions();
    const std::vector<MeshVertex>& beforeVertices = primitive.Geometry->GetVertices();
    if (beforeVertices.size() != bindPositions.size())
    {
        return SetError(errorMessage, "Geometry頂点数とSkinnedMeshData頂点数が一致しません");
    }

    // ========================================================================
    // Bind Pose verification
    // ========================================================================
    // glTFから読み込んだSkeleton / inverseBindMatrices / SkinWeightの空間が正しければ、
    // Bind Poseでは CurrentGlobal * InverseBind が各Boneについて恒等変換となり、
    // 頂点位置はImport直後から一切変化しません。
    //
    // ここをRuntime接続前に検証することで、Humanが崩れた場合にRendererではなく
    // Skeleton / inverseBind / Mesh Bind Space側の問題として切り分けられます。
    if (deformer.Deform(
            skin.SkeletonData,
            deformer.GetPose(),
            primitive.SkinningData,
            *primitive.Geometry) == false)
    {
        return SetError(errorMessage, "Bind PoseでSkeletalMeshDeformer::Deform()に失敗しました");
    }

    const std::vector<MeshVertex>& afterVertices = primitive.Geometry->GetVertices();
    if (afterVertices.size() != bindPositions.size())
    {
        return SetError(errorMessage, "Bind Pose変形後にGeometry頂点数が変化しました");
    }

    for (std::size_t vertexIndex = 0u; vertexIndex < bindPositions.size(); ++vertexIndex)
    {
        if (NearlyEqual(afterVertices[vertexIndex].Position, bindPositions[vertexIndex]) == false)
        {
            return SetError(
                errorMessage,
                "Bind Poseで頂点位置が変化しました。vertex=" + std::to_string(vertexIndex));
        }
    }

    return true;
}

} // namespace

SkeletalMeshDeformer* SkinnedMeshRuntimeAsset::GetSkeletalDeformer(
    const RuntimeSkinnedPrimitive& primitive,
    std::string* errorMessage) const
{
    if (primitive.DeformationInstance == nullptr)
    {
        SetError(errorMessage, "MeshDeformationInstanceがnullptrです");
        return nullptr;
    }

    MeshDeformer* baseDeformer = primitive.DeformationInstance->GetDeformer();
    if (baseDeformer == nullptr)
    {
        SetError(errorMessage, "MeshDeformerがnullptrです");
        return nullptr;
    }

    SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
    if (skeletalDeformer == nullptr)
    {
        SetError(errorMessage, "MeshDeformerがSkeletalMeshDeformerではありません");
        return nullptr;
    }

    return skeletalDeformer;
}

bool SkinnedMeshRuntimeAsset::Build(
    const ImportedSkinnedAsset& importedAsset,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (importedAsset.Primitives.empty())
    {
        return SetError(errorMessage, "Runtime化するSkinned Primitiveがありません");
    }

    std::vector<RuntimeSkinnedPrimitive> runtimePrimitives;
    runtimePrimitives.reserve(importedAsset.Primitives.size());

    for (const ImportedSkinnedPrimitive& importedPrimitive : importedAsset.Primitives)
    {
        if (importedPrimitive.SkinIndex >= importedAsset.Skins.size())
        {
            return SetError(errorMessage, "Primitiveが参照するSkinIndexが範囲外です");
        }
        if (importedPrimitive.Geometry == nullptr)
        {
            return SetError(errorMessage, "ImportedSkinnedPrimitive::Geometryがnullptrです");
        }

        const ImportedSkin& skin = importedAsset.Skins[importedPrimitive.SkinIndex];
        if (importedPrimitive.SkinningData.Validate(skin.SkeletonData) == false)
        {
            return SetError(errorMessage, "SkinnedMeshDataが参照Skeletonに対して不正です");
        }

        // Deformerへ渡すSkeleton / SkinnedMeshDataはRuntime Instanceごとの値です。
        // Skeleton定義そのものはcopy可能で、PoseはSkeletalMeshDeformer内部に独立して生成されます。
        Scope<SkeletalMeshDeformer> skeletalDeformer = CreateScope<SkeletalMeshDeformer>(
            skin.SkeletonData,
            importedPrimitive.SkinningData);

        if (skeletalDeformer == nullptr)
        {
            return SetError(errorMessage, "SkeletalMeshDeformerの生成に失敗しました");
        }

        // GPU Meshを生成する前にCPU Geometry上でBind Pose恒等性を確認します。
        // inverseBindMatricesやMesh Bind Spaceの解釈が間違っているAssetを、
        // 見た目が崩れた状態のままSceneへ流さないための重要なImporter/Runtime境界検証です。
        if (VerifyBindPoseGeometry(
                importedPrimitive,
                skin,
                *skeletalDeformer,
                errorMessage) == false)
        {
            return false;
        }

        Ref<Mesh> mesh = CreateRef<Mesh>(importedPrimitive.Geometry);
        if (mesh == nullptr)
        {
            return SetError(errorMessage, "Meshの生成に失敗しました");
        }

        Scope<MeshDeformer> baseDeformer = std::move(skeletalDeformer);
        Ref<MeshDeformationInstance> deformationInstance = CreateRef<MeshDeformationInstance>(
            mesh,
            std::move(baseDeformer));
        if (deformationInstance == nullptr)
        {
            return SetError(errorMessage, "MeshDeformationInstanceの生成に失敗しました");
        }

        RuntimeSkinnedPrimitive runtimePrimitive{};
        runtimePrimitive.MeshName = importedPrimitive.MeshName;
        runtimePrimitive.NodeIndex = importedPrimitive.NodeIndex;
        runtimePrimitive.MeshIndex = importedPrimitive.MeshIndex;
        runtimePrimitive.PrimitiveIndex = importedPrimitive.PrimitiveIndex;
        runtimePrimitive.MaterialIndex = importedPrimitive.MaterialIndex;
        runtimePrimitive.SkinIndex = importedPrimitive.SkinIndex;
        runtimePrimitive.WorldTransform = importedPrimitive.WorldTransform;
        runtimePrimitive.MeshInstance = std::move(mesh);
        runtimePrimitive.DeformationInstance = std::move(deformationInstance);

        runtimePrimitives.emplace_back(std::move(runtimePrimitive));
    }

    m_Primitives = std::move(runtimePrimitives);
    return true;
}

bool SkinnedMeshRuntimeAsset::LoadFromGlb(
    const std::string& filePath,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    ImportedSkinnedAsset importedAsset;
    if (SkinnedMeshImporter::LoadFromGlb(filePath, importedAsset, errorMessage) == false)
    {
        return false;
    }

    return Build(importedAsset, errorMessage);
}

bool SkinnedMeshRuntimeAsset::ResetSkinToBindPose(
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    bool found = false;
    for (const RuntimeSkinnedPrimitive& primitive : m_Primitives)
    {
        if (primitive.SkinIndex != skinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        deformer->GetPose().ResetToBindPose(deformer->GetSkeleton());
        found = true;
    }

    if (found == false)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    return true;
}

bool SkinnedMeshRuntimeAsset::SetBoneLocalTransform(
    std::size_t skinIndex,
    const std::string& boneName,
    const BoneTransform& transform,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    struct Target
    {
        SkeletalMeshDeformer* Deformer = nullptr;
        BoneIndex Index = InvalidBoneIndex;
    };

    std::vector<Target> targets;

    // まず全対象でBoneを解決してから変更します。
    // 途中のPrimitiveだけPose変更される半端な状態を作らないため、検証と変更を分離します。
    for (const RuntimeSkinnedPrimitive& primitive : m_Primitives)
    {
        if (primitive.SkinIndex != skinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        const BoneIndex boneIndex = deformer->GetSkeleton().FindBone(boneName);
        if (boneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, "指定Bone名がSkeletonにありません: " + boneName);
        }

        targets.push_back(Target{ deformer, boneIndex });
    }

    if (targets.empty())
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    for (const Target& target : targets)
    {
        if (target.Deformer == nullptr)
        {
            return SetError(errorMessage, "Bone操作対象Deformerがnullptrです");
        }

        SkeletonPose& pose = target.Deformer->GetPose();
        if (pose.SetLocalTransform(target.Index, transform) == false)
        {
            return SetError(errorMessage, "SkeletonPose::SetLocalTransform()に失敗しました");
        }
        if (pose.UpdateGlobalTransforms(target.Deformer->GetSkeleton()) == false)
        {
            return SetError(errorMessage, "SkeletonPose::UpdateGlobalTransforms()に失敗しました");
        }
    }

    return true;
}

bool SkinnedMeshRuntimeAsset::SetBoneLocalRotation(
    std::size_t skinIndex,
    const std::string& boneName,
    const math::Quat& rotation,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    // Quaternionがゼロ長だと回転行列を正しく構築できないため、操作入口で拒否します。
    const float lengthSquared = rotation.LengthSq();
    if (std::isfinite(lengthSquared) == false || lengthSquared <= math::Epsilon)
    {
        return SetError(errorMessage, "Bone Rotationが有効なQuaternionではありません");
    }

    bool found = false;
    BoneTransform localTransform{};

    // 同一Skinを参照するPrimitiveのPoseはこのAssetが同期管理します。
    // 最初のPrimitiveから現在Local Transformを取得し、Translation / Scaleを維持したまま
    // Rotationだけを差し替え、その完成値をSetBoneLocalTransform()で全Primitiveへ配布します。
    for (const RuntimeSkinnedPrimitive& primitive : m_Primitives)
    {
        if (primitive.SkinIndex != skinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        const BoneIndex boneIndex = deformer->GetSkeleton().FindBone(boneName);
        if (boneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, "指定Bone名がSkeletonにありません: " + boneName);
        }

        localTransform = deformer->GetPose().GetLocalTransform(boneIndex);
        found = true;
        break;
    }

    if (found == false)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    localTransform.Rotation = rotation.Normalized();
    return SetBoneLocalTransform(skinIndex, boneName, localTransform, errorMessage);
}

bool SkinnedMeshRuntimeAsset::SetBoneLocalRotationOffsetFromBind(
    std::size_t skinIndex,
    const std::string& boneName,
    const math::Quat& rotationOffset,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const float lengthSquared = rotationOffset.LengthSq();
    if (std::isfinite(lengthSquared) == false || lengthSquared <= math::Epsilon)
    {
        return SetError(errorMessage, "Bone Rotation Offsetが有効なQuaternionではありません");
    }

    bool found = false;
    BoneTransform bindTransform{};

    // Bind Pose基準の手動操作では「現在Pose」を元にしません。
    // 連続フレームでdeltaを適用すると回転が累積してしまうため、毎回Skeleton定義の
    // BindLocalTransformから完成Local Transformを再構築します。
    for (const RuntimeSkinnedPrimitive& primitive : m_Primitives)
    {
        if (primitive.SkinIndex != skinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        const BoneIndex boneIndex = deformer->GetSkeleton().FindBone(boneName);
        if (boneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, "指定Bone名がSkeletonにありません: " + boneName);
        }

        bindTransform = deformer->GetSkeleton().GetBone(boneIndex).BindLocalTransform;
        found = true;
        break;
    }

    if (found == false)
    {
        return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
    }

    // Bone Local Space上で追加回転を与えるため、Bind Rotationの右側へDeltaを合成します。
    // これによりImporterが復元したT-Pose方向を保持しながら、Bone自身のローカル軸で操作できます。
    bindTransform.Rotation = (bindTransform.Rotation * rotationOffset.Normalized()).Normalized();
    return SetBoneLocalTransform(skinIndex, boneName, bindTransform, errorMessage);
}

bool SkinnedMeshRuntimeAsset::GetBoneNames(
    std::size_t skinIndex,
    std::vector<std::string>& outBoneNames,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    for (const RuntimeSkinnedPrimitive& primitive : m_Primitives)
    {
        if (primitive.SkinIndex != skinIndex)
        {
            continue;
        }

        SkeletalMeshDeformer* deformer = GetSkeletalDeformer(primitive, errorMessage);
        if (deformer == nullptr)
        {
            return false;
        }

        const std::vector<Bone>& bones = deformer->GetSkeleton().GetBones();
        outBoneNames.clear();
        outBoneNames.reserve(bones.size());
        for (const Bone& bone : bones)
        {
            outBoneNames.emplace_back(bone.Name);
        }

        return true;
    }

    outBoneNames.clear();
    return SetError(errorMessage, "指定SkinIndexを参照するRuntime Primitiveがありません");
}

bool SkinnedMeshRuntimeAsset::Update(float deltaTime, std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (std::isfinite(deltaTime) == false || deltaTime < 0.0f)
    {
        return SetError(errorMessage, "deltaTimeは0以上の有限値である必要があります");
    }

    for (const RuntimeSkinnedPrimitive& primitive : m_Primitives)
    {
        if (primitive.MeshInstance == nullptr || primitive.DeformationInstance == nullptr)
        {
            return SetError(errorMessage, "Runtime Skinned PrimitiveのInstanceがnullptrです");
        }

        // MeshDeformationInstance::Update()内でSkeletalMeshDeformer::Update()が呼ばれ、
        // CPU Geometry変形後にMesh::SyncGeometry()まで既存の変形経路で処理されます。
        primitive.DeformationInstance->Update(deltaTime);
    }

    return true;
}

} // namespace Gltf
} // namespace Raven
