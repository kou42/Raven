// Raven/Gltf/StaticSceneSpawner.cpp
#include "Raven/Gltf/StaticSceneSpawner.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Gltf/GltfLitMaterialBridge.h"
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
    return math::Vec3{ value.x / scalar, value.y / scalar, value.z / scalar };
}

float Determinant3x3(const math::Vec3& c0, const math::Vec3& c1, const math::Vec3& c2)
{
    return c0.x * (c1.y * c2.z - c1.z * c2.y)
        - c1.x * (c0.y * c2.z - c0.z * c2.y)
        + c2.x * (c0.y * c1.z - c0.z * c1.y);
}

bool DecomposeWorldTransform(const math::Mat4& matrix, TransformComponent& outTransform, std::string* errorMessage)
{
    constexpr float AffineTolerance = 1.0e-4f;
    constexpr float ScaleTolerance = 1.0e-6f;
    constexpr float OrthogonalTolerance = 2.0e-4f;

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

    if (std::isfinite(scaleX) == false || std::isfinite(scaleY) == false || std::isfinite(scaleZ) == false
        || scaleX <= ScaleTolerance || scaleY <= ScaleTolerance || scaleZ <= ScaleTolerance)
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
    if (std::isfinite(determinant) == false || std::fabs(determinant - 1.0f) > 1.0e-3f)
    {
        return SetError(errorMessage, "glTF Static Mesh Node TransformのReflection/負Scaleは現段階では未対応です");
    }

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
        const float signY = clampedSinY >= 0.0f ? 1.0f : -1.0f;
        rotationX = std::atan2(signY * r10, r11);
    }

    if (std::isfinite(rotationX) == false || std::isfinite(rotationY) == false || std::isfinite(rotationZ) == false)
    {
        return SetError(errorMessage, "glTF Static Mesh Node TransformのEuler変換に失敗しました");
    }

    outTransform.Position = math::Vec3{ matrix[0][3], matrix[1][3], matrix[2][3] };
    outTransform.Rotation = math::Vec3{ rotationX, rotationY, rotationZ };
    outTransform.Scale = math::Vec3{ scaleX, scaleY, scaleZ };
    return true;
}

std::string BuildPrimitiveEntityName(const ImportedStaticMeshInstance& imported, std::size_t instanceIndex)
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

bool SpawnImportedInstances(Scene& scene, const std::vector<ImportedStaticMeshInstance>& importedInstances,
    const Ref<Material>& fallbackMaterial, bool useImportedMaterials, const DirectionalLightSettings& light,
    StaticSceneInstance& outInstance, std::string* errorMessage)
{
    if (importedInstances.empty())
    {
        return SetError(errorMessage, "Sceneへ配置するStatic Mesh Primitiveがありません");
    }

    StaticSceneInstance spawnedInstance;
    spawnedInstance.GetPrimitives().reserve(importedInstances.size());

    for (std::size_t instanceIndex = 0u; instanceIndex < importedInstances.size(); ++instanceIndex)
    {
        const ImportedStaticMeshInstance& imported = importedInstances[instanceIndex];
        if (imported.Geometry == nullptr)
        {
            StaticSceneSpawner::Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Imported Static PrimitiveのGeometryがnullptrです");
        }

        TransformComponent importedTransform{};
        if (DecomposeWorldTransform(imported.WorldTransform, importedTransform, errorMessage) == false)
        {
            StaticSceneSpawner::Destroy(scene, spawnedInstance);
            return false;
        }

        Ref<Material> primitiveMaterial = fallbackMaterial;
        if (useImportedMaterials)
        {
            if (imported.Material != nullptr)
            {
                primitiveMaterial = GltfLitMaterialBridge::Create(*imported.Material, light);
            }
            else
            {
                primitiveMaterial = LitMaterialFactory::CreateDirectionalLit(light);
            }
        }

        if (primitiveMaterial == nullptr)
        {
            StaticSceneSpawner::Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Static Primitive用Lit Materialの生成に失敗しました");
        }

        Ref<Mesh> mesh = CreateRef<Mesh>(imported.Geometry);
        if (mesh == nullptr || mesh->GetVertexArray() == nullptr)
        {
            StaticSceneSpawner::Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Imported GeometryからStatic Meshの生成に失敗しました");
        }

        Entity entity = scene.CreateEntity(BuildPrimitiveEntityName(imported, instanceIndex));
        if (static_cast<bool>(entity) == false)
        {
            StaticSceneSpawner::Destroy(scene, spawnedInstance);
            return SetError(errorMessage, "Static Primitive Entityの生成に失敗しました");
        }

        entity.GetComponent<TransformComponent>() = importedTransform;
        entity.AddComponent<MeshRendererComponent>(MeshRendererComponent{ mesh, primitiveMaterial });

        SpawnedStaticPrimitive spawnedPrimitive{};
        spawnedPrimitive.EntityHandle = entity;
        spawnedPrimitive.NodeIndex = imported.NodeIndex;
        spawnedPrimitive.MeshIndex = imported.MeshIndex;
        spawnedPrimitive.PrimitiveIndex = imported.PrimitiveIndex;
        spawnedPrimitive.MaterialIndex = imported.MaterialIndex;
        spawnedInstance.GetPrimitives().emplace_back(spawnedPrimitive);
    }

    outInstance = std::move(spawnedInstance);
    return true;
}
} // namespace

bool StaticSceneSpawner::SpawnFromGlb(Scene& scene, const std::string& filePath, const Ref<Material>& material,
    StaticSceneInstance& outInstance, std::string* errorMessage)
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
    return SpawnImportedInstances(scene, importedInstances, material, false, DirectionalLightSettings{}, outInstance, errorMessage);
}

bool StaticSceneSpawner::SpawnLitFromGlb(Scene& scene, const std::string& filePath, StaticSceneInstance& outInstance,
    const DirectionalLightSettings& light, std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    std::vector<ImportedStaticMeshInstance> importedInstances;
    if (StaticSceneImporter::LoadFromGlb(filePath, importedInstances, errorMessage) == false)
    {
        return false;
    }

    return SpawnImportedInstances(scene, importedInstances, nullptr, true, light, outInstance, errorMessage);
}

void StaticSceneSpawner::Destroy(Scene& scene, StaticSceneInstance& instance)
{
    for (const SpawnedStaticPrimitive& primitive : instance.GetPrimitives())
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
    instance.GetPrimitives().clear();
}

TerrainStaticSceneLayer::TerrainStaticSceneLayer(
    Scene& scene,
    std::string modelPath,
    const DirectionalLightSettings& light)
    : m_Scene(scene)
    , m_ModelPath(std::move(modelPath))
    , m_Light(light)
{
}

void TerrainStaticSceneLayer::OnDetach()
{
    // LayerがSceneより先にDetachされる通常ライフサイクルで、Spawnerが生成したPrimitiveを
    // 一括破棄します。Entity個別のLifetimeをLayer側へ複製管理しません。
    StaticSceneSpawner::Destroy(m_Scene, m_TerrainInstance);
    m_LoadAttempted = false;
    m_LastError.clear();
}

void TerrainStaticSceneLayer::OnUpdate(float dt)
{
    (void)dt;

    if (m_LoadAttempted == true || m_TerrainInstance.IsValid())
    {
        return;
    }

    TryLoadTerrain();
}

bool TerrainStaticSceneLayer::TryLoadTerrain()
{
    m_LoadAttempted = true;
    m_LastError.clear();

    // Scene構築時点ではRenderer/OpenGL初期化順に依存しないよう、Human Debug Layerと同じく
    // 最初のUpdateで実Assetを解決します。Terrain.glb未配置は開発途中の正常状態として扱います。
    const std::filesystem::path resolvedPath = std::filesystem::absolute(m_ModelPath);
    if (std::filesystem::exists(resolvedPath) == false)
    {
        std::cout
            << "[TerrainStaticScene] " << m_ModelPath << "\n"
            << "  解決パス: " << resolvedPath << "\n"
            << " が見つからないためTerrain読込をskipします。\n";
        return false;
    }

    if (StaticSceneSpawner::SpawnLitFromGlb(
            m_Scene,
            m_ModelPath,
            m_TerrainInstance,
            m_Light,
            &m_LastError) == false)
    {
        std::cerr
            << "[TerrainStaticScene] Terrain GLBのScene配置に失敗しました: "
            << m_LastError << '\n';
        return false;
    }

    // Terrain固有の責務として、描画PrimitiveへStaticMesh Colliderを接続します。
    // GenericなStaticSceneSpawnerは背景オブジェクトや装飾Meshにも再利用するため、
    // すべてのStatic SceneへColliderを自動付与する設計にはしません。
    //
    // ColliderはRendererのGPU Meshではなく同じCPU MeshGeometryを共有します。
    // これにより描画とPhysicsで頂点/Indexを二重所有せず、Broad Phase AABBと
    // Triangle RayCast / 後続Capsule-vs-Triangleへ同じGeometryを渡せます。
    for (SpawnedStaticPrimitive& primitive : m_TerrainInstance.GetPrimitives())
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || m_Scene.IsEntityAlive(primitive.EntityHandle) == false)
        {
            continue;
        }

        MeshRendererComponent* meshRenderer =
            m_Scene.TryGetComponent<MeshRendererComponent>(primitive.EntityHandle.GetIndex());
        if (meshRenderer == nullptr || meshRenderer->Mesh == nullptr)
        {
            StaticSceneSpawner::Destroy(m_Scene, m_TerrainInstance);
            return SetError(&m_LastError, "Terrain Primitiveの描画Meshが見つかりません");
        }

        const Ref<MeshGeometry>& geometry = meshRenderer->Mesh->GetGeometry();
        if (geometry == nullptr)
        {
            StaticSceneSpawner::Destroy(m_Scene, m_TerrainInstance);
            return SetError(&m_LastError, "Terrain PrimitiveのMeshGeometryがnullptrです");
        }

        ColliderComponent collider{};
        collider.Type = ColliderType::StaticMesh;
        collider.StaticMeshGeometry = geometry;
        primitive.EntityHandle.AddComponent<ColliderComponent>(std::move(collider));
    }

    std::cout
        << "[TerrainStaticScene] Terrain GLBを読み込みました: "
        << m_ModelPath << " (Primitive="
        << m_TerrainInstance.GetPrimitives().size() << ")\n";
    return true;
}

} // namespace Gltf
} // namespace Raven
