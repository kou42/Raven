// Raven/Gltf/SkinnedMeshSceneSpawner.cpp
#include "Raven/Gltf/SkinnedMeshSceneSpawner.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Gltf/GltfCoordinateSystem.h"
#include "Raven/Gltf/SkinnedMeshRuntime.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

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

    // TransformComponentはAffine TRSだけを表現します。
    // Perspective成分を含む4x4行列を無理に分解するとScene上の見た目が静かに壊れるため、
    // glTF Node World Matrixの最下段が[0,0,0,1]であることを先に確認します。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        return SetError(errorMessage, "glTF Mesh Node TransformがAffine TRSではありません");
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
        return SetError(errorMessage, "glTF Mesh Node TransformのScaleが特異です");
    }

    const math::Vec3 axisX = Divide(column0, scaleX);
    const math::Vec3 axisY = Divide(column1, scaleY);
    const math::Vec3 axisZ = Divide(column2, scaleZ);

    // T * R * Sでは正規化後の3列は互いに直交します。
    // Shearを含む行列はTransformComponentへ完全には保存できないため、近似せず拒否します。
    if (std::fabs(Dot(axisX, axisY)) > OrthogonalTolerance
        || std::fabs(Dot(axisX, axisZ)) > OrthogonalTolerance
        || std::fabs(Dot(axisY, axisZ)) > OrthogonalTolerance)
    {
        return SetError(errorMessage, "glTF Mesh Node TransformにShearが含まれています");
    }

    const float determinant = Determinant3x3(axisX, axisY, axisZ);
    if (std::isfinite(determinant) == false
        || std::fabs(determinant - 1.0f) > 1.0e-3f)
    {
        // Reflection / 負Scaleは、どの軸へ符号を戻すかの規約を決めないとEuler値が不安定になります。
        // Human表示の最初の段階では黙って符号を推測せず、対応範囲を明示します。
        return SetError(errorMessage, "glTF Mesh Node TransformのReflection/負Scaleは現段階では未対応です");
    }

    // 正規化した回転行列RをRavenのRotation順 Rx * Ry * Rz へ戻します。
    // Components.cppのTransformComponent::GetTransform()と厳密に逆の規約を使用します。
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
        // Gimbal LockではX/Zを一意に分離できません。
        // Z=0を代表解とし、Rを再現するXを選びます。
        const float signY = clampedSinY >= 0.0f ? 1.0f : -1.0f;
        rotationX = std::atan2(signY * r10, r11);
        rotationZ = 0.0f;
    }

    if (std::isfinite(rotationX) == false
        || std::isfinite(rotationY) == false
        || std::isfinite(rotationZ) == false)
    {
        return SetError(errorMessage, "glTF Mesh Node TransformのEuler変換に失敗しました");
    }

    outTransform.Position = math::Vec3{ matrix[0][3], matrix[1][3], matrix[2][3] };
    outTransform.Rotation = math::Vec3{ rotationX, rotationY, rotationZ };
    outTransform.Scale = math::Vec3{ scaleX, scaleY, scaleZ };
    return true;
}

std::string BuildPrimitiveEntityName(
    const RuntimeSkinnedPrimitive& primitive,
    std::size_t runtimePrimitiveIndex)
{
    std::string name = primitive.MeshName;
    if (name.empty())
    {
        name = "SkinnedMesh";
    }

    name += "_Primitive_" + std::to_string(runtimePrimitiveIndex);
    return name;
}

} // namespace

bool SkinnedMeshSceneInstance::SetBoneLocalRotation(
    std::size_t skinIndex,
    const std::string& boneName,
    const math::Quat& rotation,
    std::string* errorMessage)
{
    if (m_RuntimeAsset == nullptr)
    {
        return SetError(errorMessage, "SkinnedMeshSceneInstanceのRuntimeAssetがnullptrです");
    }

    return m_RuntimeAsset->SetBoneLocalRotation(
        skinIndex,
        boneName,
        rotation,
        errorMessage);
}

bool SkinnedMeshSceneInstance::SetBoneLocalRotationOffsetFromBind(
    std::size_t skinIndex,
    const std::string& boneName,
    const math::Quat& rotationOffset,
    std::string* errorMessage)
{
    if (m_RuntimeAsset == nullptr)
    {
        return SetError(errorMessage, "SkinnedMeshSceneInstanceのRuntimeAssetがnullptrです");
    }

    return m_RuntimeAsset->SetBoneLocalRotationOffsetFromBind(
        skinIndex,
        boneName,
        rotationOffset,
        errorMessage);
}

bool SkinnedMeshSceneInstance::GetBoneNames(
    std::size_t skinIndex,
    std::vector<std::string>& outBoneNames,
    std::string* errorMessage) const
{
    if (m_RuntimeAsset == nullptr)
    {
        outBoneNames.clear();
        return SetError(errorMessage, "SkinnedMeshSceneInstanceのRuntimeAssetがnullptrです");
    }

    return m_RuntimeAsset->GetBoneNames(skinIndex, outBoneNames, errorMessage);
}

bool SkinnedMeshSceneInstance::ResetSkinToBindPose(
    std::size_t skinIndex,
    std::string* errorMessage)
{
    if (m_RuntimeAsset == nullptr)
    {
        return SetError(errorMessage, "SkinnedMeshSceneInstanceのRuntimeAssetがnullptrです");
    }

    return m_RuntimeAsset->ResetSkinToBindPose(skinIndex, errorMessage);
}

bool SkinnedMeshSceneSpawner::SpawnFromGlb(
    Scene& scene,
    const std::string& filePath,
    const Ref<Material>& material,
    SkinnedMeshSceneInstance& outInstance,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (material == nullptr)
    {
        return SetError(errorMessage, "Skinned Mesh表示用Materialがnullptrです");
    }

    Ref<SkinnedMeshRuntimeAsset> runtimeAsset = CreateRef<SkinnedMeshRuntimeAsset>();
    if (runtimeAsset == nullptr)
    {
        return SetError(errorMessage, "SkinnedMeshRuntimeAssetの生成に失敗しました");
    }

    if (runtimeAsset->LoadFromGlb(filePath, errorMessage) == false)
    {
        return false;
    }

    const std::vector<RuntimeSkinnedPrimitive>& runtimePrimitives = runtimeAsset->GetPrimitives();
    if (runtimePrimitives.empty())
    {
        return SetError(errorMessage, "Sceneへ配置するSkinned Primitiveがありません");
    }

    SkinnedMeshSceneInstance spawnedInstance;
    spawnedInstance.m_RuntimeAsset = runtimeAsset;
    spawnedInstance.m_Primitives.reserve(runtimePrimitives.size());

    // glTFの基準座標系からRaven World Spaceへの変換はScene配置境界で一度だけ適用します。
    // 現在は両者とも+Y upのためIdentityですが、Node/Skinの座標契約をAABB推測へ依存させず、
    // 将来Raven側のWorld基底を変更する場合もこの共通変換だけを差し替えられるようにします。
    const math::Mat4 gltfToRavenWorld = BuildGltfToRavenWorldTransform();

    for (std::size_t primitiveIndex = 0u; primitiveIndex < runtimePrimitives.size(); ++primitiveIndex)
    {
        const RuntimeSkinnedPrimitive& primitive = runtimePrimitives[primitiveIndex];
        if (primitive.MeshInstance == nullptr || primitive.DeformationInstance == nullptr)
        {
            Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Runtime PrimitiveのMesh/DeformationInstanceがnullptrです");
        }

        // primitive.WorldTransformはglTF Node階層をすべて合成したScene Space行列です。
        // Authoring ToolがZ-upであった場合のExport補正もNode Transformへ含まれるため、
        // Geometry Boundsを見て追加回転を推測せず、この行列を正規経路として使用します。
        const math::Mat4 ravenWorldTransform = gltfToRavenWorld * primitive.WorldTransform;

        TransformComponent importedTransform{};
        if (DecomposeWorldTransform(
                ravenWorldTransform,
                importedTransform,
                errorMessage) == false)
        {
            Destroy(scene, spawnedInstance);
            return false;
        }

        Entity entity = scene.CreateEntity(BuildPrimitiveEntityName(primitive, primitiveIndex));
        if (static_cast<bool>(entity) == false)
        {
            Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Skinned Primitive Entityの生成に失敗しました");
        }

        // CreateEntity()が既にTransformComponentを持つため、新規Componentを重複追加せず上書きします。
        // このTransformはglTF NodeのWorld Transformを表し、MeshGeometry/SkinningDataはMesh Local Spaceを維持します。
        entity.GetComponent<TransformComponent>() = importedTransform;

        entity.AddComponent<MeshRendererComponent>(
            MeshRendererComponent{ primitive.MeshInstance, material });

        MeshDeformationComponent deformation{};
        deformation.Instance = primitive.DeformationInstance;
        deformation.Enabled = true;
        entity.AddComponent<MeshDeformationComponent>(deformation);

        SpawnedSkinnedPrimitive spawnedPrimitive{};
        spawnedPrimitive.EntityHandle = entity;
        spawnedPrimitive.NodeIndex = primitive.NodeIndex;
        spawnedPrimitive.MeshIndex = primitive.MeshIndex;
        spawnedPrimitive.PrimitiveIndex = primitive.PrimitiveIndex;
        spawnedPrimitive.SkinIndex = primitive.SkinIndex;
        spawnedInstance.m_Primitives.emplace_back(spawnedPrimitive);
    }

    outInstance = std::move(spawnedInstance);
    return true;
}

void SkinnedMeshSceneSpawner::Destroy(
    Scene& scene,
    SkinnedMeshSceneInstance& instance)
{
    for (const SpawnedSkinnedPrimitive& primitive : instance.m_Primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false)
        {
            continue;
        }

        if (scene.IsEntityAlive(primitive.EntityHandle) == false)
        {
            continue;
        }

        scene.DestroyEntity(primitive.EntityHandle);
    }

    instance.m_Primitives.clear();
    instance.m_RuntimeAsset.reset();
}

} // namespace Gltf
} // namespace Raven
