// Raven/Gltf/StaticSceneImporter.cpp
#include "Raven/Gltf/StaticSceneImporter.h"

#include <utility>

#include "Raven/Gltf/GltfCoordinateSystem.h"
#include "Raven/Gltf/NodeHierarchy.h"
#include "Raven/Gltf/StaticMeshImporter.h"

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

bool AppendNodeInstances(
    std::size_t nodeIndex,
    const NodeHierarchy& hierarchy,
    const std::vector<math::Mat4>& globalTransforms,
    const math::Mat4& gltfToRavenWorld,
    const std::vector<ImportedStaticPrimitive>& primitives,
    const std::vector<Ref<ImportedMaterial>>& materials,
    std::vector<ImportedStaticMeshInstance>& outInstances,
    std::string* errorMessage)
{
    const std::vector<Node>& nodes = hierarchy.GetNodes();
    if (nodeIndex >= nodes.size() || nodeIndex >= globalTransforms.size())
    {
        return SetError(errorMessage, "StaticSceneImporterのNode indexが範囲外です");
    }

    const Node& node = nodes[nodeIndex];
    if (node.MeshIndex != InvalidGltfIndex)
    {
        bool foundMeshPrimitive = false;
        for (const ImportedStaticPrimitive& primitive : primitives)
        {
            if (primitive.MeshIndex != node.MeshIndex)
            {
                continue;
            }

            ImportedStaticMeshInstance instance;
            instance.Geometry = primitive.Geometry;
            instance.WorldTransform = gltfToRavenWorld * globalTransforms[nodeIndex];
            instance.NodeName = node.Name;
            instance.MeshName = primitive.MeshName;
            instance.NodeIndex = nodeIndex;
            instance.MeshIndex = primitive.MeshIndex;
            instance.PrimitiveIndex = primitive.PrimitiveIndex;
            instance.MaterialIndex = primitive.MaterialIndex;

            if (primitive.MaterialIndex != InvalidGltfIndex)
            {
                if (primitive.MaterialIndex >= materials.size())
                {
                    return SetError(
                        errorMessage,
                        "Mesh PrimitiveのMaterial indexがImport済みMaterial範囲外です");
                }
                instance.Material = materials[primitive.MaterialIndex];
            }

            outInstances.emplace_back(std::move(instance));
            foundMeshPrimitive = true;
        }

        if (foundMeshPrimitive == false)
        {
            return SetError(errorMessage, "nodes[" + std::to_string(nodeIndex) + "].mesh が存在しないMeshを参照しています");
        }
    }

    for (std::size_t childIndex : node.Children)
    {
        if (AppendNodeInstances(childIndex, hierarchy, globalTransforms, gltfToRavenWorld, primitives, materials, outInstances, errorMessage) == false)
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool StaticSceneImporter::LoadFromGlb(
    const std::string& filePath,
    std::vector<ImportedStaticMeshInstance>& outInstances,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    std::vector<ImportedStaticPrimitive> primitives;
    if (StaticMeshImporter::LoadFromGlb(filePath, primitives, errorMessage) == false)
    {
        return false;
    }

    // MaterialもScene Import境界で一度だけ読み込み、PrimitiveのMaterialIndexを実体へ解決します。
    // Importer同士をRenderer Materialへ直結しないことで、Asset意味情報と描画Pass契約を分離します。
    std::vector<ImportedMaterial> importedMaterials;
    if (MaterialImporter::LoadFromGlb(filePath, importedMaterials, errorMessage) == false)
    {
        return false;
    }

    std::vector<Ref<ImportedMaterial>> materials;
    materials.reserve(importedMaterials.size());
    for (ImportedMaterial& importedMaterial : importedMaterials)
    {
        materials.emplace_back(CreateRef<ImportedMaterial>(std::move(importedMaterial)));
    }

    NodeHierarchy hierarchy;
    if (NodeHierarchy::LoadFromGlb(filePath, hierarchy, errorMessage) == false)
    {
        return false;
    }

    std::vector<math::Mat4> globalTransforms;
    if (hierarchy.BuildGlobalTransforms(globalTransforms, errorMessage) == false)
    {
        return false;
    }

    const std::vector<Node>& nodes = hierarchy.GetNodes();
    const std::vector<Scene>& scenes = hierarchy.GetScenes();
    std::vector<std::size_t> rootNodes;
    const std::size_t defaultSceneIndex = hierarchy.GetDefaultSceneIndex();

    if (defaultSceneIndex != InvalidGltfIndex)
    {
        if (defaultSceneIndex >= scenes.size())
        {
            return SetError(errorMessage, "Default Scene indexが範囲外です");
        }
        rootNodes = scenes[defaultSceneIndex].RootNodes;
    }
    else if (scenes.empty() == false)
    {
        rootNodes = scenes[0].RootNodes;
    }
    else
    {
        for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
        {
            if (nodes[nodeIndex].ParentIndex == InvalidGltfIndex)
            {
                rootNodes.emplace_back(nodeIndex);
            }
        }
    }

    const math::Mat4 gltfToRavenWorld = BuildGltfToRavenWorldTransform();
    std::vector<ImportedStaticMeshInstance> instances;
    for (std::size_t rootNodeIndex : rootNodes)
    {
        if (AppendNodeInstances(rootNodeIndex, hierarchy, globalTransforms, gltfToRavenWorld, primitives, materials, instances, errorMessage) == false)
        {
            return false;
        }
    }

    outInstances = std::move(instances);
    return true;
}

} // namespace Gltf
} // namespace Raven
