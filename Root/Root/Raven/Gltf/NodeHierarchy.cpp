// Raven/Gltf/NodeHierarchy.cpp
#include "Raven/Gltf/NodeHierarchy.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "Raven/Gltf/GlbReader.h"
#include "Raven/Gltf/JsonParser.h"

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

bool ReadSize(const JsonValue& value, std::size_t& outValue)
{
    if (value.IsNumber() == false)
    {
        return false;
    }

    const double number = value.GetNumber();
    if (std::isfinite(number) == false || number < 0.0 || std::floor(number) != number)
    {
        return false;
    }

    if (number > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }

    outValue = static_cast<std::size_t>(number);
    return true;
}

bool ReadFloat(const JsonValue& value, float& outValue)
{
    if (value.IsNumber() == false)
    {
        return false;
    }

    const double number = value.GetNumber();
    if (std::isfinite(number) == false)
    {
        return false;
    }

    const double maxFloat = static_cast<double>((std::numeric_limits<float>::max)());
    if (number < -maxFloat || number > maxFloat)
    {
        return false;
    }

    outValue = static_cast<float>(number);
    return true;
}

bool ReadFloatArray(
    const JsonValue& value,
    std::size_t expectedCount,
    std::vector<float>& outValues,
    const std::string& context,
    std::string* errorMessage)
{
    if (value.IsArray() == false)
    {
        return SetError(errorMessage, context + " はArrayである必要があります");
    }

    const JsonValue::Array& array = value.GetArray();
    if (array.size() != expectedCount)
    {
        return SetError(
            errorMessage,
            context + " の要素数は" + std::to_string(expectedCount) + "である必要があります");
    }

    outValues.resize(expectedCount);
    for (std::size_t i = 0u; i < expectedCount; ++i)
    {
        if (ReadFloat(array[i], outValues[i]) == false)
        {
            return SetError(errorMessage, context + " に有限floatへ変換できない値があります");
        }
    }

    return true;
}

bool ReadOptionalIndex(
    const JsonValue& object,
    const char* key,
    std::size_t& outValue,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
    {
        outValue = InvalidGltfIndex;
        return true;
    }

    if (ReadSize(*value, outValue) == false)
    {
        return SetError(errorMessage, context + "." + key + " は0以上の整数である必要があります");
    }

    return true;
}

bool ParseTransform(
    const JsonValue& nodeValue,
    NodeTransform& outTransform,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* matrixValue = nodeValue.Find("matrix");
    const JsonValue* translationValue = nodeValue.Find("translation");
    const JsonValue* rotationValue = nodeValue.Find("rotation");
    const JsonValue* scaleValue = nodeValue.Find("scale");

    const bool hasTrs = translationValue != nullptr
        || rotationValue != nullptr
        || scaleValue != nullptr;

    if (matrixValue != nullptr && hasTrs)
    {
        return SetError(errorMessage, context + " はmatrixとTRSを同時指定できません");
    }

    NodeTransform transform;

    if (matrixValue != nullptr)
    {
        std::vector<float> values;
        if (ReadFloatArray(*matrixValue, 16u, values, context + ".matrix", errorMessage) == false)
        {
            return false;
        }

        // glTFの16要素配列はcolumn-majorです。
        // Raven::Mat4はrow-major storageなので[row][column]へ転置して格納します。
        // 数学的な変換そのものはcolumn-vector style同士なので、意味上のMatrixは同一です。
        transform.LocalMatrix = math::Mat4{
            values[0], values[4], values[8],  values[12],
            values[1], values[5], values[9],  values[13],
            values[2], values[6], values[10], values[14],
            values[3], values[7], values[11], values[15]
        };
        transform.HasExplicitTrs = false;
        outTransform = transform;
        return true;
    }

    if (translationValue != nullptr)
    {
        std::vector<float> values;
        if (ReadFloatArray(*translationValue, 3u, values, context + ".translation", errorMessage) == false)
        {
            return false;
        }
        transform.Translation = { values[0], values[1], values[2] };
    }

    if (rotationValue != nullptr)
    {
        std::vector<float> values;
        if (ReadFloatArray(*rotationValue, 4u, values, context + ".rotation", errorMessage) == false)
        {
            return false;
        }

        math::Quat rotation{ values[0], values[1], values[2], values[3] };
        const float lengthSquared = rotation.LengthSq();
        if (std::isfinite(lengthSquared) == false || lengthSquared <= math::Epsilon * math::Epsilon)
        {
            return SetError(errorMessage, context + ".rotation が有効なQuaternionではありません");
        }

        // glTF Quaternionは単位Quaternionであることが期待されます。
        // Asset側の微小な丸め誤差はImporter境界で正規化し、Runtimeへ持ち込みません。
        transform.Rotation = rotation.Normalized();
    }

    if (scaleValue != nullptr)
    {
        std::vector<float> values;
        if (ReadFloatArray(*scaleValue, 3u, values, context + ".scale", errorMessage) == false)
        {
            return false;
        }
        transform.Scale = { values[0], values[1], values[2] };
    }

    transform.LocalMatrix = math::Mat4::Translation(transform.Translation)
        * transform.Rotation.ToMat4()
        * math::Mat4::Scaling(transform.Scale);
    transform.HasExplicitTrs = true;

    outTransform = transform;
    return true;
}

bool ParseNodes(
    const JsonValue& root,
    std::vector<Node>& outNodes,
    std::string* errorMessage)
{
    const JsonValue* nodesValue = root.Find("nodes");
    if (nodesValue == nullptr)
    {
        return true;
    }
    if (nodesValue->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.nodesはArrayである必要があります");
    }

    const JsonValue::Array& nodeArray = nodesValue->GetArray();
    outNodes.resize(nodeArray.size());

    for (std::size_t nodeIndex = 0u; nodeIndex < nodeArray.size(); ++nodeIndex)
    {
        const JsonValue& nodeValue = nodeArray[nodeIndex];
        const std::string context = "nodes[" + std::to_string(nodeIndex) + "]";

        if (nodeValue.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        Node node;

        const JsonValue* nameValue = nodeValue.Find("name");
        if (nameValue != nullptr)
        {
            if (nameValue->IsString() == false)
            {
                return SetError(errorMessage, context + ".name はStringである必要があります");
            }
            node.Name = nameValue->GetString();
        }

        if (ReadOptionalIndex(nodeValue, "mesh", node.MeshIndex, context, errorMessage) == false
            || ReadOptionalIndex(nodeValue, "skin", node.SkinIndex, context, errorMessage) == false)
        {
            return false;
        }

        if (ParseTransform(nodeValue, node.Transform, context, errorMessage) == false)
        {
            return false;
        }

        const JsonValue* childrenValue = nodeValue.Find("children");
        if (childrenValue != nullptr)
        {
            if (childrenValue->IsArray() == false)
            {
                return SetError(errorMessage, context + ".children はArrayである必要があります");
            }

            const JsonValue::Array& children = childrenValue->GetArray();
            node.Children.reserve(children.size());

            for (std::size_t childPosition = 0u; childPosition < children.size(); ++childPosition)
            {
                std::size_t childIndex = InvalidGltfIndex;
                if (ReadSize(children[childPosition], childIndex) == false)
                {
                    return SetError(errorMessage, context + ".children に0以上の整数ではない値があります");
                }
                if (childIndex >= nodeArray.size())
                {
                    return SetError(errorMessage, context + ".children がnodes範囲外を参照しています");
                }
                if (childIndex == nodeIndex)
                {
                    return SetError(errorMessage, context + " が自分自身をchildに指定しています");
                }

                for (std::size_t existingChild : node.Children)
                {
                    if (existingChild == childIndex)
                    {
                        return SetError(errorMessage, context + ".children に重複Nodeがあります");
                    }
                }

                node.Children.emplace_back(childIndex);
            }
        }

        outNodes[nodeIndex] = std::move(node);
    }

    // childrenからParentIndexを逆算します。
    // 1 Nodeが複数Parentを持つGraphはglTF Node Treeとして扱えないため拒否します。
    for (std::size_t parentIndex = 0u; parentIndex < outNodes.size(); ++parentIndex)
    {
        for (std::size_t childIndex : outNodes[parentIndex].Children)
        {
            Node& child = outNodes[childIndex];
            if (child.ParentIndex != InvalidGltfIndex)
            {
                return SetError(
                    errorMessage,
                    "nodes[" + std::to_string(childIndex) + "] が複数Parentから参照されています");
            }

            child.ParentIndex = parentIndex;
        }
    }

    return true;
}

bool VisitNodeForCycleCheck(
    std::size_t nodeIndex,
    const std::vector<Node>& nodes,
    std::vector<std::uint8_t>& states,
    std::string* errorMessage)
{
    // 0=未訪問, 1=探索中, 2=完了
    if (states[nodeIndex] == 1u)
    {
        return SetError(
            errorMessage,
            "Node階層にcycleがあります。nodes[" + std::to_string(nodeIndex) + "] を再訪しました");
    }
    if (states[nodeIndex] == 2u)
    {
        return true;
    }

    states[nodeIndex] = 1u;
    for (std::size_t childIndex : nodes[nodeIndex].Children)
    {
        if (VisitNodeForCycleCheck(childIndex, nodes, states, errorMessage) == false)
        {
            return false;
        }
    }
    states[nodeIndex] = 2u;
    return true;
}

bool ValidateAcyclic(const std::vector<Node>& nodes, std::string* errorMessage)
{
    std::vector<std::uint8_t> states(nodes.size(), 0u);
    for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
    {
        if (states[nodeIndex] == 0u
            && VisitNodeForCycleCheck(nodeIndex, nodes, states, errorMessage) == false)
        {
            return false;
        }
    }

    return true;
}

bool ParseScenes(
    const JsonValue& root,
    const std::vector<Node>& nodes,
    std::vector<Scene>& outScenes,
    std::size_t& outDefaultSceneIndex,
    std::string* errorMessage)
{
    const JsonValue* scenesValue = root.Find("scenes");
    if (scenesValue == nullptr)
    {
        outDefaultSceneIndex = InvalidGltfIndex;
        return true;
    }
    if (scenesValue->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.scenesはArrayである必要があります");
    }

    const JsonValue::Array& sceneArray = scenesValue->GetArray();
    outScenes.resize(sceneArray.size());

    for (std::size_t sceneIndex = 0u; sceneIndex < sceneArray.size(); ++sceneIndex)
    {
        const JsonValue& sceneValue = sceneArray[sceneIndex];
        const std::string context = "scenes[" + std::to_string(sceneIndex) + "]";

        if (sceneValue.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        Scene scene;

        const JsonValue* nameValue = sceneValue.Find("name");
        if (nameValue != nullptr)
        {
            if (nameValue->IsString() == false)
            {
                return SetError(errorMessage, context + ".name はStringである必要があります");
            }
            scene.Name = nameValue->GetString();
        }

        const JsonValue* rootsValue = sceneValue.Find("nodes");
        if (rootsValue != nullptr)
        {
            if (rootsValue->IsArray() == false)
            {
                return SetError(errorMessage, context + ".nodes はArrayである必要があります");
            }

            for (const JsonValue& rootNodeValue : rootsValue->GetArray())
            {
                std::size_t rootNodeIndex = InvalidGltfIndex;
                if (ReadSize(rootNodeValue, rootNodeIndex) == false || rootNodeIndex >= nodes.size())
                {
                    return SetError(errorMessage, context + ".nodes に範囲外Nodeがあります");
                }
                if (nodes[rootNodeIndex].ParentIndex != InvalidGltfIndex)
                {
                    return SetError(errorMessage, context + ".nodes がParentを持つNodeをroot指定しています");
                }

                for (std::size_t existingRoot : scene.RootNodes)
                {
                    if (existingRoot == rootNodeIndex)
                    {
                        return SetError(errorMessage, context + ".nodes に重複Rootがあります");
                    }
                }

                scene.RootNodes.emplace_back(rootNodeIndex);
            }
        }

        outScenes[sceneIndex] = std::move(scene);
    }

    outDefaultSceneIndex = InvalidGltfIndex;
    const JsonValue* defaultSceneValue = root.Find("scene");
    if (defaultSceneValue != nullptr)
    {
        if (ReadSize(*defaultSceneValue, outDefaultSceneIndex) == false
            || outDefaultSceneIndex >= outScenes.size())
        {
            return SetError(errorMessage, "glTF.scene がscenes範囲外です");
        }
    }

    return true;
}

bool BuildGlobalRecursive(
    std::size_t nodeIndex,
    const std::vector<Node>& nodes,
    std::vector<math::Mat4>& globalTransforms,
    std::vector<std::uint8_t>& states,
    std::string* errorMessage)
{
    if (states[nodeIndex] == 2u)
    {
        return true;
    }
    if (states[nodeIndex] == 1u)
    {
        return SetError(errorMessage, "Global Transform構築中にNode cycleを検出しました");
    }

    states[nodeIndex] = 1u;
    const Node& node = nodes[nodeIndex];

    if (node.ParentIndex == InvalidGltfIndex)
    {
        globalTransforms[nodeIndex] = node.Transform.LocalMatrix;
    }
    else
    {
        if (node.ParentIndex >= nodes.size())
        {
            return SetError(errorMessage, "Node ParentIndexが範囲外です");
        }

        if (BuildGlobalRecursive(
                node.ParentIndex,
                nodes,
                globalTransforms,
                states,
                errorMessage) == false)
        {
            return false;
        }

        globalTransforms[nodeIndex] = globalTransforms[node.ParentIndex]
            * node.Transform.LocalMatrix;
    }

    states[nodeIndex] = 2u;
    return true;
}

} // namespace

bool NodeHierarchy::LoadFromGlb(
    const std::string& filePath,
    NodeHierarchy& outHierarchy,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    GlbData glbData;
    if (GlbReader::ReadFromFile(filePath, glbData, errorMessage) == false)
    {
        return false;
    }

    JsonValue root;
    if (JsonParser::Parse(glbData.JsonText, root, errorMessage) == false)
    {
        return false;
    }

    return BuildFromJson(root, outHierarchy, errorMessage);
}

bool NodeHierarchy::BuildFromJson(
    const JsonValue& root,
    NodeHierarchy& outHierarchy,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    if (root.IsObject() == false)
    {
        return SetError(errorMessage, "glTF RootはObjectである必要があります");
    }

    NodeHierarchy hierarchy;
    if (ParseNodes(root, hierarchy.m_Nodes, errorMessage) == false
        || ValidateAcyclic(hierarchy.m_Nodes, errorMessage) == false
        || ParseScenes(
            root,
            hierarchy.m_Nodes,
            hierarchy.m_Scenes,
            hierarchy.m_DefaultSceneIndex,
            errorMessage) == false)
    {
        return false;
    }

    outHierarchy = std::move(hierarchy);
    return true;
}

bool NodeHierarchy::BuildGlobalTransforms(
    std::vector<math::Mat4>& outGlobalTransforms,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    std::vector<math::Mat4> globalTransforms(m_Nodes.size(), math::Mat4::Identity());
    std::vector<std::uint8_t> states(m_Nodes.size(), 0u);

    for (std::size_t nodeIndex = 0u; nodeIndex < m_Nodes.size(); ++nodeIndex)
    {
        if (BuildGlobalRecursive(
                nodeIndex,
                m_Nodes,
                globalTransforms,
                states,
                errorMessage) == false)
        {
            return false;
        }
    }

    outGlobalTransforms = std::move(globalTransforms);
    return true;
}

} // namespace Gltf
} // namespace Raven
