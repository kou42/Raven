// Raven/Gltf/Debug/HumanSkinningDebugLayer.cpp
#include "Raven/Gltf/Debug/HumanSkinningDebugLayer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
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
#include "Raven/Scene/SceneGame.h"

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
    return math::Vec3{
        value.x / scalar,
        value.y / scalar,
        value.z / scalar
    };
}

float Determinant3x3(
    const math::Vec3& column0,
    const math::Vec3& column1,
    const math::Vec3& column2)
{
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

    // ========================================================================
    // Humanoid直立補正後のWorld MatrixをSceneのTRSへ戻す
    // ========================================================================
    // 直立補正は各Primitiveの既存World Transformの左側へ同じ行列として適用します。
    // TransformComponentは行列そのものを保持せずTRSを保持するため、補正後Matrixを
    // Position / Rotation / Scaleへ分解して書き戻します。
    //
    // ShearやReflectionを「近いTRS」へ丸めるとBody / Clothes間の相対配置が静かに壊れるため、
    // Ravenの現在のTransformComponentで正確に表現できないMatrixは明示的に拒否します。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformがAffine TRSではありません");
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
        return SetError(errorMessage, "Humanoid直立補正後TransformのScaleが特異です");
    }

    const math::Vec3 axisX = Divide(column0, scaleX);
    const math::Vec3 axisY = Divide(column1, scaleY);
    const math::Vec3 axisZ = Divide(column2, scaleZ);

    if (std::fabs(Dot(axisX, axisY)) > OrthogonalTolerance
        || std::fabs(Dot(axisX, axisZ)) > OrthogonalTolerance
        || std::fabs(Dot(axisY, axisZ)) > OrthogonalTolerance)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformにShearが含まれています");
    }

    const float determinant = Determinant3x3(axisX, axisY, axisZ);
    if (std::isfinite(determinant) == false
        || std::fabs(determinant - 1.0f) > 1.0e-3f)
    {
        // Reflection / 負Scaleは符号をどの軸へ戻すかの規約が必要です。
        // Debug表示のために推測せず、現段階では対応範囲外とします。
        return SetError(errorMessage, "Humanoid直立補正後TransformのReflection/負Scaleは未対応です");
    }

    // TransformComponent::GetTransform()の Rx * Ry * Rz 規約へ戻します。
    const float r00 = axisX.x;
    const float r10 = axisX.y;
    const float r01 = axisY.x;
    const float r11 = axisY.y;
    const float r02 = axisZ.x;
    const float r12 = axisZ.y;
    const float r22 = axisZ.z;

    const float clampedSinY = r02 < -1.0f ? -1.0f : (r02 > 1.0f ? 1.0f : r02);
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
        // Gimbal LockではX/Zを一意に分離できないため、Z=0を代表解にします。
        const float signY = clampedSinY >= 0.0f ? 1.0f : -1.0f;
        rotationX = std::atan2(signY * r10, r11);
        rotationZ = 0.0f;
    }

    if (std::isfinite(rotationX) == false
        || std::isfinite(rotationY) == false
        || std::isfinite(rotationZ) == false)
    {
        return SetError(errorMessage, "Humanoid直立補正後TransformのEuler変換に失敗しました");
    }

    outTransform.Position = math::Vec3{ matrix[0][3], matrix[1][3], matrix[2][3] };
    outTransform.Rotation = math::Vec3{ rotationX, rotationY, rotationZ };
    outTransform.Scale = math::Vec3{ scaleX, scaleY, scaleZ };
    return true;
}

std::string NormalizeBoneName(const std::string& source)
{
    // Exporterごとの "mixamorig:Hips" / "Armature_Hips" 等の差を吸収する比較用表現です。
    // 元のBone名自体は変更せず、英数字だけを残して小文字化します。
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

    // 完全一致を先に調べ、HeadTop等をHeadとして誤認しないようにします。
    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeBoneName(candidate);
        for (BoneIndex boneIndex = 0u;
             boneIndex < static_cast<BoneIndex>(bones.size());
             ++boneIndex)
        {
            if (NormalizeBoneName(bones[boneIndex].Name) == normalizedCandidate)
            {
                return boneIndex;
            }
        }
    }

    // namespace / Armature prefix等が付いた場合だけsuffix一致をfallbackとして使います。
    for (const std::string& candidate : candidates)
    {
        const std::string normalizedCandidate = NormalizeBoneName(candidate);
        for (BoneIndex boneIndex = 0u;
             boneIndex < static_cast<BoneIndex>(bones.size());
             ++boneIndex)
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
    math::Vec3& outSkeletonParentUp,
    math::Vec3& outMeshLocalUp,
    math::Vec3& outWorldUp,
    std::string& outLowerBoneName,
    std::string& outUpperBoneName,
    std::string* errorMessage)
{
    // ========================================================================
    // 1. Runtime Skinningが実際に使っているSkeleton / Bind Spaceを正規データにする
    // ========================================================================
    // 以前はNodeHierarchy上のHips -> Head Global方向をそのままHuman Upとしていました。
    // しかしglTF Skinningでは、Mesh Bind SpaceとJoint/Skeleton側の空間が一致するとは限りません。
    // Raven自身もSkeletalMeshDeformerでその差を
    //
    //   Skeleton Parent Space -> Mesh Local Space
    //
    // の補正行列として復元しています。横倒しの見た目を正しく判定するには、この補正を通した
    // Hips -> Head方向を使う必要があります。これによりAABB推測へ戻らず、Skinningの数学契約から
    // 人物がMesh内でどちらを向いているかを求められます。
    const Ref<SkinnedMeshRuntimeAsset>& runtimeAsset = instance.GetRuntimeAsset();
    if (runtimeAsset == nullptr)
    {
        return SetError(errorMessage, "Humanoid直立判定用RuntimeAssetがnullptrです");
    }

    const std::vector<RuntimeSkinnedPrimitive>& runtimePrimitives = runtimeAsset->GetPrimitives();
    for (const RuntimeSkinnedPrimitive& runtimePrimitive : runtimePrimitives)
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
        if (baseDeformer == nullptr)
        {
            continue;
        }

        SkeletalMeshDeformer* skeletalDeformer = dynamic_cast<SkeletalMeshDeformer*>(baseDeformer);
        if (skeletalDeformer == nullptr)
        {
            continue;
        }

        if (skeletalDeformer->GetBindSpaceCorrectionError().empty() == false)
        {
            return SetError(
                errorMessage,
                "Humanoid直立判定に必要なBind Space補正が無効です: "
                    + skeletalDeformer->GetBindSpaceCorrectionError());
        }

        const Skeleton& skeleton = skeletalDeformer->GetSkeleton();
        const BoneIndex lowerBoneIndex = FindBoneBySemanticName(skeleton, { "Hips", "Pelvis" });
        const BoneIndex upperBoneIndex = FindBoneBySemanticName(skeleton, { "Head" });

        if (lowerBoneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, "Humanoid直立判定に必要なHips/Pelvis Boneを解決できませんでした");
        }
        if (upperBoneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, "Humanoid直立判定に必要なHead Boneを解決できませんでした");
        }

        // ControllerがPoseを変更する前の初期化段階ですが、現在Poseへ依存させないため
        // 専用のBind Poseを作り、Skeleton定義からGlobal Transformを再構築します。
        SkeletonPose bindPose;
        bindPose.ResetToBindPose(skeleton);

        const math::Vec3 lowerSkeletonParent = TransformPosition(
            bindPose.GetGlobalTransform(lowerBoneIndex),
            math::Vec3{ 0.0f, 0.0f, 0.0f });
        const math::Vec3 upperSkeletonParent = TransformPosition(
            bindPose.GetGlobalTransform(upperBoneIndex),
            math::Vec3{ 0.0f, 0.0f, 0.0f });

        const math::Vec3 skeletonParentVector = upperSkeletonParent - lowerSkeletonParent;
        const float skeletonParentLength = skeletonParentVector.Length();
        if (std::isfinite(skeletonParentLength) == false || skeletonParentLength <= 1.0e-5f)
        {
            return SetError(errorMessage, "Skeleton Parent SpaceのHips/Pelvis -> Head方向が0に近すぎます");
        }
        outSkeletonParentUp = skeletonParentVector / skeletonParentLength;

        // SkeletalMeshDeformerがSkinning時に使用するものと同じBind Space補正です。
        // Joint位置にもこの変換を通すことで、Skeleton上では+YでもMesh Localでは+Zというような
        // 「SkeletonとGeometryの基底差」を明示的に反映できます。
        const math::Mat4& skeletonParentToMesh =
            skeletalDeformer->GetSkeletonParentToMeshTransform();
        const math::Vec3 lowerMeshLocal = TransformPosition(skeletonParentToMesh, lowerSkeletonParent);
        const math::Vec3 upperMeshLocal = TransformPosition(skeletonParentToMesh, upperSkeletonParent);

        const math::Vec3 meshLocalVector = upperMeshLocal - lowerMeshLocal;
        const float meshLocalLength = meshLocalVector.Length();
        if (std::isfinite(meshLocalLength) == false || meshLocalLength <= 1.0e-5f)
        {
            return SetError(errorMessage, "Mesh Local SpaceのHips/Pelvis -> Head方向が0に近すぎます");
        }
        outMeshLocalUp = meshLocalVector / meshLocalLength;

        // 最後に、実際にRendererへ渡るEntity World Transformも通します。
        // ここで得た方向が「現在画面に描かれるHumanの意味的な上方向」です。
        const TransformComponent& transform =
            spawnedPrimitive->EntityHandle.GetComponent<TransformComponent>();
        const math::Mat4 worldTransform = transform.GetTransform();
        const math::Vec3 lowerWorld = TransformPosition(worldTransform, lowerMeshLocal);
        const math::Vec3 upperWorld = TransformPosition(worldTransform, upperMeshLocal);

        const math::Vec3 worldVector = upperWorld - lowerWorld;
        const float worldLength = worldVector.Length();
        if (std::isfinite(worldLength) == false || worldLength <= 1.0e-5f)
        {
            return SetError(errorMessage, "Raven World SpaceのHips/Pelvis -> Head方向が0に近すぎます");
        }
        outWorldUp = worldVector / worldLength;

        const math::Vec3 targetUp{ 0.0f, 1.0f, 0.0f };
        const float rawDot = math::Vec3::Dot(outWorldUp, targetUp);
        const float clampedDot = rawDot < -1.0f ? -1.0f : (rawDot > 1.0f ? 1.0f : rawDot);

        // ====================================================================
        // 2. 実際の描画上UpをRaven +Yへ向ける最短回転を作る
        // ====================================================================
        if (clampedDot >= 1.0f - 1.0e-5f)
        {
            outRotation = math::Mat4::Identity();
        }
        else if (clampedDot <= -1.0f + 1.0e-5f)
        {
            // 完全な反対向きではCross軸が0になるため+Xを180度回転軸として使用します。
            outRotation = math::Quat::FromAxisAngle(
                math::Vec3{ 1.0f, 0.0f, 0.0f },
                3.14159265358979323846f).ToMat4();
        }
        else
        {
            const math::Vec3 rotationAxis = math::Vec3::Cross(outWorldUp, targetUp).Normalized();
            const float rotationAngle = std::acos(clampedDot);
            outRotation = math::Quat::FromAxisAngle(rotationAxis, rotationAngle).ToMat4();
        }

        outLowerBoneName = skeleton.GetBone(lowerBoneIndex).Name;
        outUpperBoneName = skeleton.GetBone(upperBoneIndex).Name;
        return true;
    }

    return SetError(errorMessage, "Humanoid直立判定に利用できるSkeletalMeshDeformerがありません");
}

bool ApplyHumanoidUprightRotation(
    std::vector<SpawnedSkinnedPrimitive>& primitives,
    const math::Mat4& uprightRotation,
    std::string* errorMessage)
{
    // ========================================================================
    // Human全体へ共通のWorld Space直立回転を適用
    // ========================================================================
    // 各PrimitiveのLocal Transformを個別に推測補正せず、Spawn済みWorld Transformの左側へ
    // 同じ回転を掛けます。
    //
    //   M_correctedWorld = R_upright * M_importedWorld
    //
    // Skeleton / inverseBindMatrices / Mesh Local Space自体は変更しないため、Skinningの数学は維持しつつ
    // Human全体のScene配置方向だけを修正できます。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        const math::Mat4 correctedWorld = uprightRotation * transform.GetTransform();

        // Rotation.x等への角度加算は既存Node Rotationとの積順を壊すため、行列として左乗算した後に
        // RavenのTRS規約へ戻します。
        TransformComponent correctedTransform{};
        if (DecomposeWorldTransform(correctedWorld, correctedTransform, errorMessage) == false)
        {
            return false;
        }

        transform = correctedTransform;
    }

    return true;
}

bool NormalizeHumanForDebugView(
    const SkinnedMeshSceneInstance& instance,
    std::vector<SpawnedSkinnedPrimitive>& primitives,
    std::string* errorMessage)
{
    constexpr float TargetHeight = 20.0f;
    constexpr float MinimumHeight = 1.0e-5f;

    // ========================================================================
    // 1. Skinning Bind SpaceからHuman全体の直立回転を決定して適用
    // ========================================================================
    // AABBの長軸ではなく、Runtime Skinningが実際に使用している
    // Skeleton Parent -> Mesh Local -> Entity Worldの変換経路を通したHips/Pelvis -> Headを使います。
    // これにより、Skeleton Nodeだけを見ると+YでもGeometry側では+Zへ寝ているAssetを正しく扱えます。
    math::Mat4 uprightRotation = math::Mat4::Identity();
    math::Vec3 skeletonParentUp{};
    math::Vec3 meshLocalUp{};
    math::Vec3 worldUp{};
    std::string lowerBoneName;
    std::string upperBoneName;
    if (BuildHumanoidUprightRotation(
            instance,
            primitives,
            uprightRotation,
            skeletonParentUp,
            meshLocalUp,
            worldUp,
            lowerBoneName,
            upperBoneName,
            errorMessage) == false)
    {
        return false;
    }

    if (ApplyHumanoidUprightRotation(primitives, uprightRotation, errorMessage) == false)
    {
        return false;
    }

    const float maxFloat = std::numeric_limits<float>::max();
    math::Vec3 sourceBoundsMin{ maxFloat, maxFloat, maxFloat };
    math::Vec3 sourceBoundsMax{ -maxFloat, -maxFloat, -maxFloat };
    bool hasVertex = false;

    // ========================================================================
    // 2. 直立補正後のWorld AABBを計算
    // ========================================================================
    // ここでAABBを使う目的は方向判定ではなく、表示サイズ・Center・Floor合わせだけです。
    // 各PrimitiveのMesh Local頂点へEntity World Transformを適用し、共通World AABBへ統合します。
    for (const SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false
            || primitive.EntityHandle.HasComponent<MeshRendererComponent>() == false)
        {
            continue;
        }

        const TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        const MeshRendererComponent& meshRenderer = primitive.EntityHandle.GetComponent<MeshRendererComponent>();

        if (meshRenderer.Mesh == nullptr || meshRenderer.Mesh->GetGeometry() == nullptr)
        {
            continue;
        }

        const math::Mat4 worldTransform = transform.GetTransform();
        const std::vector<MeshVertex>& vertices = meshRenderer.Mesh->GetGeometry()->GetVertices();
        for (const MeshVertex& vertex : vertices)
        {
            const math::Vec3 worldPosition = TransformPosition(worldTransform, vertex.Position);
            sourceBoundsMin.x = std::min(sourceBoundsMin.x, worldPosition.x);
            sourceBoundsMin.y = std::min(sourceBoundsMin.y, worldPosition.y);
            sourceBoundsMin.z = std::min(sourceBoundsMin.z, worldPosition.z);
            sourceBoundsMax.x = std::max(sourceBoundsMax.x, worldPosition.x);
            sourceBoundsMax.y = std::max(sourceBoundsMax.y, worldPosition.y);
            sourceBoundsMax.z = std::max(sourceBoundsMax.z, worldPosition.z);
            hasVertex = true;
        }
    }

    if (hasVertex == false)
    {
        return SetError(errorMessage, "Human Debug表示用Boundsを計算できませんでした");
    }

    const math::Vec3 sourceBoundsCenter = (sourceBoundsMin + sourceBoundsMax) * 0.5f;
    const math::Vec3 sourceBoundsSize = sourceBoundsMax - sourceBoundsMin;
    const float sourceHeight = sourceBoundsSize.y;
    if (sourceHeight <= MinimumHeight)
    {
        return SetError(errorMessage, "Skinning Bind Space基準の直立補正後Human高さが0に近すぎます");
    }

    // ========================================================================
    // 3. 高さをTargetHeightへUniform Scaleし、Center / Floorを正規化
    // ========================================================================
    // Uniform Scaleだけを使い、Humanの縦横比やPrimitive間の相対Scaleを維持します。
    const float uniformScale = TargetHeight / sourceHeight;
    const math::Vec3 debugTranslation{
        -sourceBoundsCenter.x * uniformScale,
        -sourceBoundsMin.y * uniformScale,
        -sourceBoundsCenter.z * uniformScale
    };

    // ========================================================================
    // 4. Human全体へ同じWorld Space Scale / Translationを適用
    // ========================================================================
    // 直立回転は既に完了しているためRotationには触れず、Scene上の表示位置とサイズだけを揃えます。
    for (SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            continue;
        }

        TransformComponent& transform = primitive.EntityHandle.GetComponent<TransformComponent>();
        transform.Position = debugTranslation + transform.Position * uniformScale;
        transform.Scale = transform.Scale * uniformScale;
    }

    // Skeleton空間・Mesh空間・World空間の方向を並べて出すことで、将来別Assetで問題が起きても
    // 「どの境界で軸が変わったか」をAABB推測なしで追跡できます。
    std::cout
        << "[HumanSkinning] Debug Bounds after Skinning-space upright alignment:\n"
        << "  Min    = (" << sourceBoundsMin.x << ", " << sourceBoundsMin.y << ", " << sourceBoundsMin.z << ")\n"
        << "  Max    = (" << sourceBoundsMax.x << ", " << sourceBoundsMax.y << ", " << sourceBoundsMax.z << ")\n"
        << "  Size   = (" << sourceBoundsSize.x << ", " << sourceBoundsSize.y << ", " << sourceBoundsSize.z << ")\n"
        << "  Coordinate System = " << GetGltfCoordinateSystemDescription() << '\n'
        << "  Humanoid Up Source = " << lowerBoneName << " -> " << upperBoneName << '\n'
        << "  Skeleton Parent Up = ("
        << skeletonParentUp.x << ", " << skeletonParentUp.y << ", " << skeletonParentUp.z << ")\n"
        << "  Mesh Local Up = ("
        << meshLocalUp.x << ", " << meshLocalUp.y << ", " << meshLocalUp.z << ")\n"
        << "  World Up Before Correction = ("
        << worldUp.x << ", " << worldUp.y << ", " << worldUp.z << ")\n"
        << "  Humanoid Up Target = (0, 1, 0)\n"
        << "  AABB used for orientation = false\n"
        << "  Debug Scale = " << uniformScale << '\n';

    return true;
}

} // namespace

HumanSkinningDebugLayer::~HumanSkinningDebugLayer()
{
    // Human EntityはこのLayerが生成責務を持つため、Layer終了時にSpawnerへ破棄を委譲します。
    // SceneGame側の汎用Entity一覧へLifetimeを移譲しないことで、生成者と破棄責務を一致させます。
    DestroyHuman();
}

bool HumanSkinningDebugLayer::TryInitialize()
{
    if (m_Initialized == true)
    {
        return true;
    }

    if (m_InitializationAttempted == true)
    {
        return false;
    }

    // LayerはSceneGame constructorから登録されますが、Human Mesh/Material生成はOnCreate後の
    // 最初のUpdateまで遅延します。これによりOpenGL/Pipeline初期化順へ依存しません。
    if (m_Scene.m_Material == nullptr)
    {
        return false;
    }

    m_InitializationAttempted = true;

    // 相対パスの場合、カレントワーキングディレクトリから解決されます。
    std::filesystem::path resolvedPath = std::filesystem::absolute(m_ModelPath);

    if (std::filesystem::exists(resolvedPath) == false)
    {
        std::cout
            << "[HumanSkinning] " << m_ModelPath << "\n"
            << "  解決パス: " << resolvedPath << "\n"
            << "  カレントディレクトリ: " << std::filesystem::current_path() << "\n"
            << " が見つからないためHuman検証をskipします。\n";
        return false;
    }

    std::string errorMessage;
    if (SkinnedMeshSceneSpawner::SpawnFromGlb(
            m_Scene,
            m_ModelPath,
            m_Scene.m_Material,
            m_HumanInstance,
            &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Human.glbのScene配置に失敗しました: "
            << errorMessage << '\n';
        return false;
    }

    // Debug表示正規化ではPrimitive EntityのTransformComponentを書き換えるため、
    // const参照ではなくSceneInstanceが所有する配列への非const参照が必要です。
    // GetPrimitives()のnon-const overloadを使うことでconst_castを避け、所有権境界を維持します。
    std::vector<SpawnedSkinnedPrimitive>& primitives = m_HumanInstance.GetPrimitives();
    if (primitives.empty())
    {
        std::cerr << "[HumanSkinning] Spawn後のPrimitiveが0件です。\n";
        DestroyHuman();
        return false;
    }

    // ========================================================================
    // Human Debug固有の直立補正と表示正規化
    // ========================================================================
    // glTFの+Y upはScene座標系の規約であり、Mesh Bind Space内のHumanが+Yへ直立している保証では
    // ありません。今回はSkeletalMeshDeformerが復元済みのBind Space補正を利用して
    //
    //   Skeleton Parent -> Mesh Local -> Entity World
    //
    // の順にHips/Pelvis -> Head方向を追跡し、最終的に画面へ出るHumanのUpだけをRaven +Yへ揃えます。
    // AABBは直立方向の判定には使用せず、直立後の表示サイズ・Center・Floor合わせだけに使います。
    if (NormalizeHumanForDebugView(m_HumanInstance, primitives, &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Human.glbのSkinning Bind Space基準Debug正規化に失敗しました: "
            << errorMessage << '\n';
        DestroyHuman();
        return false;
    }

    const std::size_t skinIndex = primitives.front().SkinIndex;
    if (m_Controller.Initialize(m_HumanInstance, skinIndex, &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Debug Controllerの初期化に失敗しました: "
            << errorMessage << '\n';
        DestroyHuman();
        return false;
    }

    // Human Primitiveは通常のTransformComponent + MeshRendererComponentを持つため、
    // SceneGame::RenderScene()のECS Viewへ自動的に参加します。
    // LifetimeについてもSkinnedMeshSceneInstanceがPrimitive Handleを保持しているので、
    // SceneGameへ別途Handleを複製せず、このLayerのDestroyHuman()からSpawnerへ返します。

    std::cout << "[HumanSkinning] Human.glbを読み込みました。Bone一覧:\n";
    for (const std::string& boneName : m_Controller.GetBoneNames())
    {
        std::cout << "  - " << boneName << '\n';
    }

    std::cout
        << "[HumanSkinning] Controls: U/I LeftUpperArm, J/K LeftForeArm, M/L Head, R Reset\n"
        << "[HumanSkinning] Resolved LeftUpperArm: "
        << (m_Controller.GetLeftUpperArmBoneName().empty()
            ? "<not found>"
            : m_Controller.GetLeftUpperArmBoneName())
        << '\n'
        << "[HumanSkinning] Resolved LeftForeArm: "
        << (m_Controller.GetLeftForeArmBoneName().empty()
            ? "<not found>"
            : m_Controller.GetLeftForeArmBoneName())
        << '\n'
        << "[HumanSkinning] Resolved Head: "
        << (m_Controller.GetHeadBoneName().empty()
            ? "<not found>"
            : m_Controller.GetHeadBoneName())
        << '\n';

    m_Initialized = true;
    return true;
}

void HumanSkinningDebugLayer::OnUpdate(float deltaTime)
{
    // SceneGameでは現在、Layer UpdateがOnUpdateGame()とScene::OnUpdateLayer()の2経路から
    // 呼ばれます。Human debugだけ二重入力しないよう、1回目だけ処理しOnRender()で解除します。
    if (m_UpdatedSinceRender == true)
    {
        return;
    }
    m_UpdatedSinceRender = true;

    if (m_Initialized == false)
    {
        TryInitialize();
    }

    if (m_Initialized == false)
    {
        return;
    }

    std::string errorMessage;
    if (m_Controller.Update(deltaTime, &errorMessage) == false)
    {
        std::cerr
            << "[HumanSkinning] Bone手動操作に失敗しました: "
            << errorMessage << '\n';
    }
}

void HumanSkinningDebugLayer::OnRender()
{
    // 次のUpdate frameで1回だけ入力処理できるよう解除します。
    m_UpdatedSinceRender = false;
}

void HumanSkinningDebugLayer::DestroyHuman()
{
    // Human Entityの生成/破棄責務はSkinnedMeshSceneSpawnerへ対称に集約します。
    // SceneGame側はHuman PrimitiveのHandleを所有せず、描画もECS Viewが自動的に担当します。
    if (m_HumanInstance.IsValid() == true)
    {
        SkinnedMeshSceneSpawner::Destroy(m_Scene, m_HumanInstance);
    }

    m_Controller.Reset();
    m_HumanInstance = {};
    m_Initialized = false;
}

} // namespace Gltf
} // namespace Raven
