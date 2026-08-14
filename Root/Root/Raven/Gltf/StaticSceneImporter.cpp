// Raven/Gltf/StaticSceneImporter.cpp
#include "Raven/Gltf/StaticSceneImporter.h"

#include <utility>

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
    const std::vector<ImportedStaticPrimitive>& primitives,
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
            instance.WorldTransform = globalTransforms[nodeIndex];
            instance.NodeName = node.Name;
            instance.MeshName = primitive.MeshName;
            instance.NodeIndex = nodeIndex;
            instance.MeshIndex = primitive.MeshIndex;
            instance.PrimitiveIndex = primitive.PrimitiveIndex;
            instance.MaterialIndex = primitive.MaterialIndex;

            outInstances.emplace_back(std::move(instance));
            foundMeshPrimitive = true;
        }

        if (foundMeshPrimitive == false)
        {
            return SetError(
                errorMessage,
                "nodes[" + std::to_string(nodeIndex) + "].mesh が存在しないMeshを参照しています");
        }
    }

    for (std::size_t childIndex : node.Children)
    {
        if (AppendNodeInstances(
                childIndex,
                hierarchy,
                globalTransforms,
                primitives,
                outInstances,
                errorMessage) == false)
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
        // glTFのscene指定は任意です。
        // 表示用Importerとしてはscene未指定時に先頭Sceneを採用し、呼び出し側が追加選択APIを
        // 必要とする段階でSceneIndex指定版を追加します。
        rootNodes = scenes[0].RootNodes;
    }
    else
    {
        // scenes自体が無いAssetでは、Parentを持たない全NodeをRootとして扱います。
        // Mesh検証やSkeleton構築に使えるNode情報を捨てないためのfallbackです。
        for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
        {
            if (nodes[nodeIndex].ParentIndex == InvalidGltfIndex)
            {
                rootNodes.emplace_back(nodeIndex);
            }
        }
    }

    std::vector<ImportedStaticMeshInstance> instances;
    for (std::size_t rootNodeIndex : rootNodes)
    {
        if (AppendNodeInstances(
                rootNodeIndex,
                hierarchy,
                globalTransforms,
                primitives,
                instances,
                errorMessage) == false)
        {
            return false;
        }
    }

    outInstances = std::move(instances);
    return true;
}

} // namespace Gltf
} // namespace Raven
