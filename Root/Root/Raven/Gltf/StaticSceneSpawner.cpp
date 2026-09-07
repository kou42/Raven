// Raven/Gltf/StaticSceneSpawner.cpp
#include "Raven/Gltf/StaticSceneSpawner.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Gltf/StaticSceneImporter.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
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
    // Static SceneでもSkinned Meshと同じ座標契約を使い、表現できないShear等を
    // 黙って近似しないことでEditor表示とImporter結果の食い違いを防ぎます。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        return SetError(errorMessage, "glTF Static Mesh Node TransformがAffine TRSではありません");
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
        return SetError(errorMessage, "glTF Static Mesh Node TransformのScaleが特異です");
    }

    const math::Vec3 axisX = Divide(column0, scaleX);
    const math::Vec3 axisY = Divide(column1, scaleY);
    const math::Vec3 axisZ = Divide(column2, scaleZ);

    if (std::fabs(Dot(axisX, axisY)) > OrthogonalTolerance
        || std::fabs(Dot(axisX, axisZ)) > OrthogonalTolerance
        || std::fabs(Dot(axisY, axisZ)) > OrthogonalTolerance)
    {
        return SetError(errorMessage, "glTF Static Mesh Node TransformにShearが含まれています");
    }

    const float determinant = Determinant3x3(axisX, axisY, axisZ);
    if (std::isfinite(determinant) == false
        || std::fabs(determinant - 1.0f) > 1.0e-3f)
    {
        return SetError(errorMessage, "glTF Static Mesh Node TransformのReflection/負Scaleは現段階では未対応です");
    }

    // TransformComponent::GetTransform()の Rx * Ry * Rz と同じ規約へ戻します。
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
        // Gimbal LockではZ=0を代表解とし、同じ回転行列を再現するXを選びます。
        const float signY = clampedSinY >= 0.0f ? 1.0f : -1.0f;
        rotationX = std::atan2(signY * r10, r11);
        rotationZ = 0.0f;
    }

    if (std::isfinite(rotationX) == false
        || std::isfinite(rotationY) == false
        || std::isfinite(rotationZ) == false)
    {
        return SetError(errorMessage, "glTF Static Mesh Node TransformのEuler変換に失敗しました");
    }

    outTransform.Position = math::Vec3{ matrix[0][3], matrix[1][3], matrix[2][3] };
    outTransform.Rotation = math::Vec3{ rotationX, rotationY, rotationZ };
    outTransform.Scale = math::Vec3{ scaleX, scaleY, scaleZ };
    return true;
}

std::string BuildPrimitiveEntityName(
    const ImportedStaticMeshInstance& imported,
    std::size_t instanceIndex)
{
    std::string name = imported.NodeName;
    if (name.empty())
    {
        name = imported.MeshName;
    }
    if (name.empty())
    {
        name = "StaticMesh";
    }

    name += "_Primitive_" + std::to_string(instanceIndex);
    return name;
}

} // namespace

bool StaticSceneSpawner::SpawnFromGlb(
    Scene& scene,
    const std::string& filePath,
    const Ref<Material>& material,
    StaticSceneInstance& outInstance,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (material == nullptr)
    {
        return SetError(errorMessage, "Static Scene表示用Materialがnullptrです");
    }

    std::vector<ImportedStaticMeshInstance> importedInstances;
    if (StaticSceneImporter::LoadFromGlb(filePath, importedInstances, errorMessage) == false)
    {
        return false;
    }

    if (importedInstances.empty())
    {
        return SetError(errorMessage, "Sceneへ配置するStatic Mesh Primitiveがありません");
    }

    StaticSceneInstance spawnedInstance;
    spawnedInstance.m_Primitives.reserve(importedInstances.size());

    for (std::size_t instanceIndex = 0u; instanceIndex < importedInstances.size(); ++instanceIndex)
    {
        const ImportedStaticMeshInstance& imported = importedInstances[instanceIndex];
        if (imported.Geometry == nullptr)
        {
            Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Imported Static PrimitiveのGeometryがnullptrです");
        }

        TransformComponent importedTransform{};
        if (DecomposeWorldTransform(
                imported.WorldTransform,
                importedTransform,
                errorMessage) == false)
        {
            Destroy(scene, spawnedInstance);
            return false;
        }

        Ref<Mesh> mesh = CreateRef<Mesh>(imported.Geometry);
        if (mesh == nullptr || mesh->GetVertexArray() == nullptr)
        {
            Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Imported GeometryからStatic Meshの生成に失敗しました");
        }

        Entity entity = scene.CreateEntity(BuildPrimitiveEntityName(imported, instanceIndex));
        if (static_cast<bool>(entity) == false)
        {
            Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Static Primitive Entityの生成に失敗しました");
        }

        // CreateEntity()が持つTransformをglTF Node World Transformで置き換えます。
        // GeometryはMesh Local Spaceのまま共有できるため、Node Transformを頂点へBakeしません。
        entity.GetComponent<TransformComponent>() = importedTransform;
        entity.AddComponent<MeshRendererComponent>(
            MeshRendererComponent{ mesh, material });

        SpawnedStaticPrimitive spawnedPrimitive{};
        spawnedPrimitive.EntityHandle = entity;
        spawnedPrimitive.NodeIndex = imported.NodeIndex;
        spawnedPrimitive.MeshIndex = imported.MeshIndex;
        spawnedPrimitive.PrimitiveIndex = imported.PrimitiveIndex;
        spawnedPrimitive.MaterialIndex = imported.MaterialIndex;
        spawnedInstance.m_Primitives.emplace_back(spawnedPrimitive);
    }

    outInstance = std::move(spawnedInstance);
    return true;
}

void StaticSceneSpawner::Destroy(
    Scene& scene,
    StaticSceneInstance& instance)
{
    for (const SpawnedStaticPrimitive& primitive : instance.m_Primitives)
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
}

} // namespace Gltf
} // namespace Raven
