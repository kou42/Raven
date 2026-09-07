// Raven/Gltf/StaticSceneSpawner.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
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
// 共通Materialを明示する旧経路に加え、ImportedMaterialをDirectional Lit Materialへ
// 自動変換する経路を提供します。これによりTerrain GLBのbaseColorFactor/Textureを保持したまま
// Primitiveごとに適切なMaterialを割り当てられます。
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

    static void Destroy(
        Scene& scene,
        StaticSceneInstance& instance);
};

} // namespace Gltf
} // namespace Raven
