// Raven/Gltf/StaticSceneSpawner.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Material;
class Scene;

namespace Gltf
{

// ============================================================================
// SpawnedStaticPrimitive
// ============================================================================
// glTF Static Sceneの1 PrimitiveとScene Entityの対応を保持します。
// Import結果のGeometryはMeshへ変換してEntityへ所有させるため、Spawner側では
// Scene上のHandleとglTF由来indexだけを保持します。
struct SpawnedStaticPrimitive
{
    Entity EntityHandle{};

    std::size_t NodeIndex = InvalidGltfIndex;
    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t PrimitiveIndex = InvalidGltfIndex;
    std::size_t MaterialIndex = InvalidGltfIndex;
};

// ============================================================================
// StaticSceneInstance
// ============================================================================
// 1つのGLB Static SceneをScene/ECSへ展開したRuntime Handleです。
// Entity群をまとめて保持することで、Demo Sceneの差し替えやImport失敗時のrollbackを
// 呼び出し側が個別Entityを追跡せずに行えるようにします。
class StaticSceneInstance
{
public:
    bool IsValid() const
    {
        return m_Primitives.empty() == false;
    }

    std::vector<SpawnedStaticPrimitive>& GetPrimitives()
    {
        return m_Primitives;
    }

    const std::vector<SpawnedStaticPrimitive>& GetPrimitives() const
    {
        return m_Primitives;
    }

private:
    friend class StaticSceneSpawner;

    std::vector<SpawnedStaticPrimitive> m_Primitives;
};

// ============================================================================
// StaticSceneSpawner
// ============================================================================
// StaticSceneImporterのAsset変換結果をScene/ECSへ接続するBridgeです。
//
// 各Primitive Entityへ以下を設定します。
// - TransformComponent     : glTF Node World TransformをRaven TRSへ変換
// - MeshRendererComponent : Imported Geometryから生成したMesh + 指定Material
//
// ImportedMaterialはbaseColor情報まで取得できますが、Renderer Materialへの変換は
// Shader/Pipeline契約に依存します。そのため最初の段階では呼び出し側から共通Materialを受け取り、
// 次工程でglTF Material -> Raven Material Bridgeを追加できる境界を維持します。
class StaticSceneSpawner
{
public:
    static bool SpawnFromGlb(
        Scene& scene,
        const std::string& filePath,
        const Ref<Material>& material,
        StaticSceneInstance& outInstance,
        std::string* errorMessage = nullptr);

    // Sceneへ生成済みの全Primitive Entityを破棄します。
    // 途中で既に破棄済みのEntityがあっても安全にskipします。
    static void Destroy(
        Scene& scene,
        StaticSceneInstance& instance);
};

} // namespace Gltf
} // namespace Raven
