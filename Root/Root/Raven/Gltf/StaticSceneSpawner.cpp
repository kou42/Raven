// Raven/Gltf/StaticSceneSpawner.cpp
#include "Raven/Gltf/StaticSceneSpawner.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
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

math::Vec3 TransformPoint(const math::Mat4& matrix, const math::Vec3& point)
{
    const math::Vec4 transformed = matrix * math::Vec4{ point.x, point.y, point.z, 1.0f };
    return math::Vec3{ transformed.x, transformed.y, transformed.z };
}

bool ComputeLocalBounds(const MeshGeometry& geometry, math::Vec3& outMin, math::Vec3& outMax)
{
    const std::vector<MeshVertex>& vertices = geometry.GetVertices();
    if (vertices.empty())
    {
        return false;
    }

    const float maxFloat = std::numeric_limits<float>::max();
    outMin = math::Vec3{ maxFloat, maxFloat, maxFloat };
    outMax = math::Vec3{ -maxFloat, -maxFloat, -maxFloat };

    for (const MeshVertex& vertex : vertices)
    {
        outMin.x = std::min(outMin.x, vertex.Position.x);
        outMin.y = std::min(outMin.y, vertex.Position.y);
        outMin.z = std::min(outMin.z, vertex.Position.z);
        outMax.x = std::max(outMax.x, vertex.Position.x);
        outMax.y = std::max(outMax.y, vertex.Position.y);
        outMax.z = std::max(outMax.z, vertex.Position.z);
    }
    return true;
}

void ComputeWorldBounds(
    const math::Vec3& localMin,
    const math::Vec3& localMax,
    const math::Mat4& worldTransform,
    math::Vec3& outMin,
    math::Vec3& outMax)
{
    const float maxFloat = std::numeric_limits<float>::max();
    outMin = math::Vec3{ maxFloat, maxFloat, maxFloat };
    outMax = math::Vec3{ -maxFloat, -maxFloat, -maxFloat };

    // 回転・非一様Scaleを含むため、Local AABBの8頂点をWorldへ変換して包み直します。
    // min/maxだけを直接変換すると回転時にBoundsを過小評価するため、診断値でも同じ誤差を持ち込まないようにします。
    for (uint32_t cornerIndex = 0u; cornerIndex < 8u; ++cornerIndex)
    {
        const math::Vec3 localCorner{
            (cornerIndex & 1u) != 0u ? localMax.x : localMin.x,
            (cornerIndex & 2u) != 0u ? localMax.y : localMin.y,
            (cornerIndex & 4u) != 0u ? localMax.z : localMin.z
        };
        const math::Vec3 worldCorner = TransformPoint(worldTransform, localCorner);
        outMin.x = std::min(outMin.x, worldCorner.x);
        outMin.y = std::min(outMin.y, worldCorner.y);
        outMin.z = std::min(outMin.z, worldCorner.z);
        outMax.x = std::max(outMax.x, worldCorner.x);
        outMax.y = std::max(outMax.y, worldCorner.y);
        outMax.z = std::max(outMax.z, worldCorner.z);
    }
}

void PrintTerrainPrimitiveDiagnostics(
    const SpawnedStaticPrimitive& primitive,
    const TransformComponent& transform,
    const MeshRendererComponent& meshRenderer,
    const MeshGeometry& geometry)
{
    math::Vec3 localMin{};
    math::Vec3 localMax{};
    const bool hasBounds = ComputeLocalBounds(geometry, localMin, localMax);

    const std::vector<MeshVertex>& vertices = geometry.GetVertices();
    const std::vector<uint32_t>& indices = geometry.GetIndices();
    const std::size_t triangleCount = indices.empty() ? vertices.size() / 3u : indices.size() / 3u;

    std::cout
        << "[TerrainStaticScene] Primitive診断"
        << " Node=" << primitive.NodeIndex
        << " Mesh=" << primitive.MeshIndex
        << " Primitive=" << primitive.PrimitiveIndex << '\n'
        << "  Vertices=" << vertices.size()
        << " Indices=" << indices.size()
        << " Triangles=" << triangleCount << '\n'
        << "  Material=" << (meshRenderer.Material != nullptr ? "valid" : "nullptr") << '\n'
        << "  Position=(" << transform.Position.x << ", " << transform.Position.y << ", " << transform.Position.z << ")\n"
        << "  Rotation=(" << transform.Rotation.x << ", " << transform.Rotation.y << ", " << transform.Rotation.z << ") rad\n"
        << "  Scale=(" << transform.Scale.x << ", " << transform.Scale.y << ", " << transform.Scale.z << ")\n";

    if (hasBounds == false)
    {
        std::cout << "  Bounds=unavailable (vertexなし)\n";
        return;
    }

    math::Vec3 worldMin{};
    math::Vec3 worldMax{};
    ComputeWorldBounds(localMin, localMax, transform.GetTransform(), worldMin, worldMax);

    const math::Vec3 worldCenter = (worldMin + worldMax) * 0.5f;
    const math::Vec3 worldExtent = worldMax - worldMin;
    std::cout
        << "  LocalAABB Min=(" << localMin.x << ", " << localMin.y << ", " << localMin.z << ")"
        << " Max=(" << localMax.x << ", " << localMax.y << ", " << localMax.z << ")\n"
        << "  WorldAABB Min=(" << worldMin.x << ", " << worldMin.y << ", " << worldMin.z << ")"
        << " Max=(" << worldMax.x << ", " << worldMax.y << ", " << worldMax.z << ")\n"
        << "  WorldCenter=(" << worldCenter.x << ", " << worldCenter.y << ", " << worldCenter.z << ")"
        << " Size=(" << worldExtent.x << ", " << worldExtent.y << ", " << worldExtent.z << ")\n";
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

        const TransformComponent* transform =
            m_Scene.TryGetComponent<TransformComponent>(primitive.EntityHandle.GetIndex());
        if (transform == nullptr)
        {
            StaticSceneSpawner::Destroy(m_Scene, m_TerrainInstance);
            return SetError(&m_LastError, "Terrain PrimitiveのTransformが見つかりません");
        }

        // GLB読込成功だけでは「Cameraから見える位置にある」ことまでは保証できません。
        // Geometry量、TRS、Local/World AABBを一度出力し、Importer・Transform・Cameraのどこで
        // 可視性が失われているかを実Asset値から切り分けられるようにします。
        PrintTerrainPrimitiveDiagnostics(primitive, *transform, *meshRenderer, *geometry);

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
