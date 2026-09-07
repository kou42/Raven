// Raven/Gltf/StaticSceneSpawner.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Renderer/Material/LitMaterialFactory.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Material;
class Scene;

namespace Gltf
{

struct SpawnedStaticPrimitive
{
    Entity EntityHandle{};
    std::size_t NodeIndex = InvalidGltfIndex;
    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t PrimitiveIndex = InvalidGltfIndex;
    std::size_t MaterialIndex = InvalidGltfIndex;
};

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

// StaticSceneImporterのAsset変換結果をScene/ECSへ接続するBridgeです。
class StaticSceneSpawner
{
public:
    static bool SpawnFromGlb(
        Scene& scene,
        const std::string& filePath,
        const Ref<Material>& material,
        StaticSceneInstance& outInstance,
        std::string* errorMessage = nullptr);

    static bool SpawnLitFromGlb(
        Scene& scene,
        const std::string& filePath,
        StaticSceneInstance& outInstance,
        const DirectionalLightSettings& light = DirectionalLightSettings{},
        std::string* errorMessage = nullptr);

    static void Destroy(Scene& scene, StaticSceneInstance& instance);
};

// ============================================================================
// TerrainStaticSceneLayer
// ============================================================================
// Static Mesh TerrainをSceneへ接続する最小Runtime Layerです。
// Assetがまだ配置されていない開発途中でも既存Sceneを壊さないよう、最初のUpdateで一度だけ
// ファイル存在を確認し、未配置なら静かにskipします。
//
// Terrain固有のAsset PathとLighting設定はLayer側に閉じ、StaticSceneSpawnerやImporterへ
// Demo固有知識を持ち込まない設計にしています。
class TerrainStaticSceneLayer final : public Layer
{
public:
    explicit TerrainStaticSceneLayer(
        Scene& scene,
        std::string modelPath = "Raven/Assets/Models/Terrain.glb",
        const DirectionalLightSettings& light = DirectionalLightSettings{});

    void OnDetach() override;
    void OnUpdate(float dt) override;

    bool IsLoaded() const
    {
        return m_TerrainInstance.IsValid();
    }

    const std::string& GetLastError() const
    {
        return m_LastError;
    }

private:
    bool TryLoadTerrain();

    Scene& m_Scene;
    std::string m_ModelPath;
    DirectionalLightSettings m_Light{};
    StaticSceneInstance m_TerrainInstance;
    std::string m_LastError;
    bool m_LoadAttempted = false;
};

} // namespace Gltf
} // namespace Raven
