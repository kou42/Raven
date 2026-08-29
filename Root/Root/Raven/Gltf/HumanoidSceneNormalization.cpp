// Raven/Gltf/HumanoidSceneNormalization.cpp
#include "Raven/Gltf/HumanoidSceneNormalization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

#include "Raven/Animation/SkeletalMeshDeformer.h"
#include "Raven/Animation/SkeletonPose.h"
#include "Raven/Gltf/GltfCoordinateSystem.h"
#include "Raven/Gltf/SkinnedMeshRuntime.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Renderer/Mesh/Deformation/MeshDeformationInstance.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Scene/Components.h"

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

math::Vec3 TransformPosition(const math::Mat4& matrix, const math::Vec3& position)
{
    const math::Vec4 transformed = matrix * math::Vec4{ position.x, position.y, position.z, 1.0f };
    return { transformed.x, transformed.y, transformed.z };
}

float Dot(const math::Vec3& a, const math::Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Length(const math::Vec3& value)
{
    return std::sqrt(Dot(value, value));
}

math::Vec3 Divide(const math::Vec3& value, float scalar)
{
    return math::Vec3{ value.x / scalar, value.y / scalar, value.z / scalar };
}

float Determinant3x3(
    const math::Vec3& column0,
    const math::Vec3& column1,
    const math::Vec3& column2)
{
    // 3本の列ベクトルから通常の3x3 determinantを計算します。
    // Reflection/負Scale検出に使うため、各成分の参照先を取り違えると正しい回転まで拒否してしまいます。
    return column0.x * (column1.y * column2.z - column1.z * column2.y)
        - column1.x * (column0.y * column2.z - column0.z * column2.y)
        + column2.x * (column0.y * column1.z - column0.z * column1.y);
}

bool DecomposeWorldTransform(
    const math::Mat4& matrix,
    TransformComponent& outTransform,
    std::string* errorMessage)
{
    constexpr float AffineTolerance = 1.0e-4f;
    constexpr float ScaleTolerance = 1.0e-6f;
    constexpr float OrthogonalTolerance = 2.0e-4f;

    // Humanoid全Primitiveへ共通World回転を左乗算した後、RavenのTRS表現へ戻します。
    // Shear/Reflectionを近似して通すと衣服とBodyの相対Transformが静かに壊れるため、
    // 現在のTransformComponentで正確に表現できない行列は明示的に拒否します。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        return SetError(errorMessage, "Humanoid正規化後TransformがAffine TRSではありません");
    }

    const math::Vec3 column0{ matrix[0][0], matrix[1][0], matrix[2][0] };
    const math::Vec3 column1{ matrix[0][1], matrix[1][1], matrix[2][1] };
    const math::Vec3 column2{ matrix[0][2], matrix[1][2], matrix[2][2] };

    const float scaleX = Length(column0);
    const float scaleY = Length(column1);
    const float scaleZ = Length(column2);
    if (std::isfinite(scaleX) == false
        || std::isfinite(scaleY) == false
        || std::isfinite(scaleZ) == false
        || scaleX <= ScaleTolerance
        || scaleY <= ScaleTolerance
        || scaleZ <= ScaleTolerance)
    {
        return SetError(errorMessage, "Humanoid正規化後TransformのScaleが特異です");
    }

    const math::Vec3 axisX = Divide(column0, scaleX);
    const math::Vec3 axisY = Divide(column1, scaleY);
    const math::Vec3 axisZ = Divide(column2, scaleZ);
    if (std::fabs(Dot(axisX, axisY)) > OrthogonalTolerance
        || std::fabs(Dot(axisX, axisZ)) > OrthogonalTolerance
        || std::fabs(Dot(axisY, axisZ)) > OrthogonalTolerance)
    {
        return SetError(errorMessage, "Humanoid正規化後TransformにShearが含まれています");
    }

    const float determinant = Determinant3x3(axisX, axisY, axisZ);
    if (std::isfinite(determinant) == false
        || std::fabs(determinant - 1.0f) > 1.0e-3f)
    {
        return SetError(errorMessage, "Humanoid正規化後TransformのReflection/負Scaleは未対応です");
    }

    const float r00 = axisX.x;
    const float r10 = axisX.y;
    const float r01 = axisY.x;
    const float r11 = axisY.y;
    const float r02 = axisZ.x;
    const float r12 = axisZ.y;
    const float r22 = axisZ.z;

    const float clampedSinY = std::clamp(r02, -1.0f, 1.0f);
    const float rotationY = std::asin(clampedSinY);
    const float cosY = std::cos(rotationY);

    float rotationX = 0.0f;
    float rotationZ = 0.0f;
    if (std::fabs(cosY) > 1.0e-5f)
    {
        rotationX = std::atan2(-r12, r22);
        rotationZ = std::atan2(-r01, r00);
    }
    else
    {
        const float signY = clampedSinY >= 0.0f ? 1.0f : -1.0f;
        rotationX = std::atan2(signY * r10, r11);
        rotationZ = 0.0f;
    }

    if (std::isfinite(rotationX) == false
        || std::isfinite(rotationY) == false
        || std::isfinite(rotationZ) == false)
    {
        return SetError(errorMessage, "Humanoid正規化後TransformのEuler変換に失敗しました");
    }

    outTransform.Position = math::Vec3{ matrix[0][3], matrix[1][3], matrix[2][3] };
    outTransform.Rotation = math::Vec3{ rotationX, rotationY, rotationZ };
    outTransform.Scale = math::Vec3{ scaleX, scaleY, scaleZ };
    return true;
}

std::string NormalizeBoneName(const std::string& source)
{
    std::string normalized;
    normalized.reserve(source.size());
    for (const unsigned char character : source)
    {
        if (std::isalnum(character) == 0)
        {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

bool EndsWith(const std::string& value, const std::string& suffix)
{
    if (suffix.size() > value.size())
    {
        return false;
    }
    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

BoneIndex FindBoneBySemanticName(
    const Skeleton& skeleton,
    const std::vector<std::string>& candidates)
{
    const std::vector<Bone>& bones = skeleton.GetBones();
    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeBoneName(candidate);
        for (BoneIndex boneIndex = 0u; boneIndex < static_cast<BoneIndex>(bones.size()); ++boneIndex)
        {
            if (NormalizeBoneName(bones[boneIndex].Name) == normalizedCandidate)
            {
                return boneIndex;
            }
        }
    }

    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeBoneName(candidate);
        for (BoneIndex boneIndex = 0u; boneIndex < static_cast<BoneIndex>(bones.size()); ++boneIndex)
        {
            if (EndsWith(NormalizeBoneName(bones[boneIndex].Name), normalizedCandidate))
            {
                return boneIndex;
            }
        }
    }
    return InvalidBoneIndex;
}

const SpawnedSkinnedPrimitive* FindSpawnedPrimitive(
    const std::vector<SpawnedSkinnedPrimitive>& primitives,
    const RuntimeSkinnedPrimitive& runtimePrimitive)
{
    for (const SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (primitive.NodeIndex == runtimePrimitive.NodeIndex
            && primitive.MeshIndex == runtimePrimitive.MeshIndex
            && primitive.PrimitiveIndex == runtimePrimitive.PrimitiveIndex
            && primitive.SkinIndex == runtimePrimitive.SkinIndex)
        {
            return &primitive;
        }
    }
    return nullptr;
}

bool BuildHumanoidUprightRotation(
    const SkinnedMeshSceneInstance& instance,
    const std::vector<SpawnedSkinnedPrimitive>& primitives,
    math::Mat4& outRotation,
    std::string* errorMessage)
{
    const Ref<SkinnedMeshRuntimeAsset>& runtimeAsset = instance.GetRuntimeAsset();
    if (runtimeAsset == nullptr)
    {
        return SetError(errorMessage, "Humanoid直立判定用RuntimeAssetがnullptrです");
    }

    // AABBではなく、実際のSkinning契約を通したHips/Pelvis -> HeadをHuman Upとして使います。
    // Skeleton Parent -> Mesh Local -> Entity World の各境界を通すため、Exporter固有の
    // Bind Space軸差があっても画面上の意味的な上方向を取得できます。
    for (const RuntimeSkinnedPrimitive& runtimePrimitive : runtimeAsset->GetPrimitives())
    {
        if (runtimePrimitive.DeformationInstance == nullptr)
        {
            continue;
        }

        const SpawnedSkinnedPrimitive* spawnedPrimitive = FindSpawnedPrimitive(primitives, runtimePrimitive);
        if (spawnedPrimitive == nullptr
            || static_cast<bool>(spawnedPrimitive->EntityHandle) == false
            || spawnedPrimitive->EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        MeshDeformer* baseDeformer = runtimePrimitive.DeformationInstance->GetDeformer();
        SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
        if (skeletalDeformer == nullptr)
        {
            continue;
        }
        if (skeletalDeformer->GetBindSpaceCorrectionError().empty() == false)
        {
            return SetError(errorMessage, skeletalDeformer->GetBindSpaceCorrectionError());
        }

        const Skeleton& skeleton = skeletalDeformer->GetSkeleton();
        const BoneIndex lowerBoneIndex = FindBoneBySemanticName(skeleton, { "Hips", "Pelvis" });
        const BoneIndex upperBoneIndex = FindBoneBySemanticName(skeleton, { "Head" });
        if (lowerBoneIndex == InvalidBoneIndex || upperBoneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, "Humanoid直立判定に必要なHips/PelvisまたはHead Boneを解決できませんでした");
        }

        SkeletonPose bindPose;
        bindPose.ResetToBindPose(skeleton);
        const math::Vec3 lowerSkeleton = TransformPosition(bindPose.GetGlobalTransform(lowerBoneIndex), {});
        const math::Vec3 upperSkeleton = TransformPosition(bindPose.GetGlobalTransform(upperBoneIndex), {});

        const math::Mat4& skeletonToMesh = skeletalDeformer->GetSkeletonParentToMeshTransform();
        const math::Vec3 lowerMesh = TransformPosition(skeletonToMesh, lowerSkeleton);
        const math::Vec3 upperMesh = TransformPosition(skeletonToMesh, upperSkeleton);

        const TransformComponent& transform = spawnedPrimitive->EntityHandle.GetComponent<TransformComponent>();
        const math::Vec3 lowerWorld = TransformPosition(transform.GetTransform(), lowerMesh);
        const math::Vec3 upperWorld = TransformPosition(transform.GetTransform(), upperMesh);
        const math::Vec3 worldVector = upperWorld - lowerWorld;
        const float worldLength = worldVector.Length();
        if (std::isfinite(worldLength) == false || worldLength <= 1.0e-5f)
        {
            return SetError(errorMessage, "HumanoidのHips/Pelvis -> Head方向が0に近すぎます");
        }

        const math::Vec3 worldUp = worldVector / worldLength;
        const math::Vec3 targetUp{ 0.0f, 1.0f, 0.0f };
        const float dot = std::clamp(math::Vec3::Dot(worldUp, targetUp), -1.0f, 1.0f);
        if (dot >= 1.0f - 1.0e-5f)
        {
            outRotation = math::Mat4::Identity();
        }
        else if (dot <= -1.0f + 1.0e-5f)
        {
            outRotation = math::Quat::FromAxisAngle(
                math::Vec3{ 1.0f, 0.0f, 0.0f },
                3.14159265358979323846f).ToMat4();
        }
        else
        {
            const math::Vec3 axis = math::Vec3::Cross(worldUp, targetUp).Normalized();
            outRotation = math::Quat::FromAxisAngle(axis, std::acos(dot)).ToMat4();
        }
        return true;
    }

    return SetError(errorMessage, "Humanoid直立判定に利用できるSkeletalMeshDeformerがありません");
}

} // namespace

bool NormalizeHumanoidSceneInstance(
    const SkinnedMeshSceneInstance& instance,
    std::vector<SpawnedSkinnedPrimitive>& primitives,
    float targetHeight,
    std::string* errorMessage)
{
    constexpr float MinimumHeight = 1.0e-5f;
    if (std::isfinite(targetHeight) == false || targetHeight <= MinimumHeight)
    {
        return SetError(errorMessage, "Humanoid正規化のtargetHeightが不正です");
    }
    if (primitives.empty())
    {
        return SetError(errorMessage, "Humanoid正規化対象Primitiveが0件です");
    }

    math::Mat4 uprightRotation = math::Mat4::Identity();
    if (BuildHumanoidUprightRotation(instance, primitives, uprightRotation, errorMessage) == false)
    {
        return false;
    }

    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        TransformComponent corrected{};
        if (DecomposeWorldTransform(uprightRotation * transform.GetTransform(), corrected, errorMessage) == false)
        {
            return false;
        }
        transform = corrected;
    }

    const float maxFloat = std::numeric_limits<float>::max();
    math::Vec3 boundsMin{ maxFloat, maxFloat, maxFloat };
    math::Vec3 boundsMax{ -maxFloat, -maxFloat, -maxFloat };
    bool hasVertex = false;

    for (const SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false
            || primitive.EntityHandle.HasComponent<MeshRendererComponent>() == false)
        {
            continue;
        }

        const TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        const MeshRendererComponent& renderer = primitive.EntityHandle.GetComponent<MeshRendererComponent>();
        if (renderer.Mesh == nullptr || renderer.Mesh->GetGeometry() == nullptr)
        {
            continue;
        }

        for (const MeshVertex& vertex : renderer.Mesh->GetGeometry()->GetVertices())
        {
            const math::Vec3 world = TransformPosition(transform.GetTransform(), vertex.Position);
            boundsMin.x = std::min(boundsMin.x, world.x);
            boundsMin.y = std::min(boundsMin.y, world.y);
            boundsMin.z = std::min(boundsMin.z, world.z);
            boundsMax.x = std::max(boundsMax.x, world.x);
            boundsMax.y = std::max(boundsMax.y, world.y);
            boundsMax.z = std::max(boundsMax.z, world.z);
            hasVertex = true;
        }
    }

    if (hasVertex == false)
    {
        return SetError(errorMessage, "Humanoid正規化用Boundsを計算できませんでした");
    }

    const math::Vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
    const float sourceHeight = boundsMax.y - boundsMin.y;
    if (std::isfinite(sourceHeight) == false || sourceHeight <= MinimumHeight)
    {
        return SetError(errorMessage, "Humanoid正規化前の身長が0に近すぎます");
    }

    const float uniformScale = targetHeight / sourceHeight;
    const math::Vec3 translation{
        -boundsCenter.x * uniformScale,
        -boundsMin.y * uniformScale,
        -boundsCenter.z * uniformScale
    };

    // ここで全Primitiveへ同じUniform Scale/Translationを適用します。
    // 結果としてHumanの足元中央が原点、高さがtargetHeightになり、呼び出し側はこの状態を
    // Character Rootに対するVisual Local Poseとして保存できます。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        transform.Position = translation + transform.Position * uniformScale;
        transform.Scale = transform.Scale * uniformScale;
    }

    std::cout
        << "[HumanoidNormalization] targetHeight=" << targetHeight
        << ", sourceHeight=" << sourceHeight
        << ", uniformScale=" << uniformScale
        << ", coordinateSystem=" << GetGltfCoordinateSystemDescription() << '\n';
    return true;
}

} // namespace Gltf
} // namespace Raven
