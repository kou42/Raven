// Raven/Gltf/SkinImporter.cpp
#include "Raven/Gltf/SkinImporter.h"

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Animation/Bone.h"
#include "Raven/Gltf/AccessorReader.h"
#include "Raven/Gltf/GlbReader.h"
#include "Raven/Gltf/JsonParser.h"
#include "Raven/Gltf/NodeHierarchy.h"

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
    if (std::isfinite(number) == false
        || number < 0.0
        || std::floor(number) != number
        || number > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }

    outValue = static_cast<std::size_t>(number);
    return true;
}

bool IsNearlyIdentity(const math::Mat4& matrix, float tolerance = 1.0e-5f)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const float expected = row == column ? 1.0f : 0.0f;
            const float difference = matrix[row][column] - expected;
            if (difference < -tolerance || difference > tolerance)
            {
                return false;
            }
        }
    }

    return true;
}

bool ParseJointIndices(
    const JsonValue& skinValue,
    std::size_t nodeCount,
    const std::string& context,
    std::vector<std::size_t>& outJointNodes,
    std::string* errorMessage)
{
    const JsonValue* joints = skinValue.Find("joints");
    if (joints == nullptr || joints->IsArray() == false)
    {
        return SetError(errorMessage, context + ".joints Arrayがありません");
    }

    const JsonValue::Array& array = joints->GetArray();
    if (array.empty())
    {
        return SetError(errorMessage, context + ".joints が空です");
    }

    std::vector<bool> used(nodeCount, false);
    outJointNodes.clear();
    outJointNodes.reserve(array.size());

    for (std::size_t jointSlot = 0u; jointSlot < array.size(); ++jointSlot)
    {
        std::size_t nodeIndex = InvalidGltfIndex;
        if (ReadSize(array[jointSlot], nodeIndex) == false)
        {
            return SetError(
                errorMessage,
                context + ".joints[" + std::to_string(jointSlot) + "] が0以上の整数ではありません");
        }
        if (nodeIndex >= nodeCount)
        {
            return SetError(
                errorMessage,
                context + ".joints[" + std::to_string(jointSlot) + "] がNode範囲外です");
        }
        if (used[nodeIndex])
        {
            return SetError(errorMessage, context + ".joints に同じNodeが重複しています");
        }

        used[nodeIndex] = true;
        outJointNodes.emplace_back(nodeIndex);
    }

    return true;
}

bool ParseOptionalSkinNodeIndex(
    const JsonValue& skinValue,
    const char* key,
    std::size_t nodeCount,
    const std::string& context,
    std::size_t& outNodeIndex,
    std::string* errorMessage)
{
    const JsonValue* value = skinValue.Find(key);
    if (value == nullptr)
    {
        outNodeIndex = InvalidGltfIndex;
        return true;
    }

    if (ReadSize(*value, outNodeIndex) == false || outNodeIndex >= nodeCount)
    {
        return SetError(errorMessage, context + "." + key + " がNode範囲外です");
    }

    return true;
}

bool ParseOptionalAccessorIndex(
    const JsonValue& skinValue,
    const char* key,
    std::size_t accessorCount,
    const std::string& context,
    std::size_t& outAccessorIndex,
    std::string* errorMessage)
{
    const JsonValue* value = skinValue.Find(key);
    if (value == nullptr)
    {
        outAccessorIndex = InvalidGltfIndex;
        return true;
    }

    if (ReadSize(*value, outAccessorIndex) == false || outAccessorIndex >= accessorCount)
    {
        return SetError(errorMessage, context + "." + key + " がAccessor範囲外です");
    }

    return true;
}

bool ValidateJointHierarchy(
    const NodeHierarchy& hierarchy,
    const std::vector<std::size_t>& jointNodes,
    const std::vector<math::Mat4>& globalTransforms,
    const std::string& context,
    std::vector<std::size_t>& outRootJointNodes,
    std::vector<std::size_t>& outNodeToJointSlot,
    std::string* errorMessage)
{
    const std::vector<Node>& nodes = hierarchy.GetNodes();
    outNodeToJointSlot.assign(nodes.size(), InvalidGltfIndex);

    for (std::size_t jointSlot = 0u; jointSlot < jointNodes.size(); ++jointSlot)
    {
        outNodeToJointSlot[jointNodes[jointSlot]] = jointSlot;
    }

    outRootJointNodes.clear();

    for (std::size_t jointSlot = 0u; jointSlot < jointNodes.size(); ++jointSlot)
    {
        const std::size_t nodeIndex = jointNodes[jointSlot];
        const Node& node = nodes[nodeIndex];

        // Raven::BoneTransformはTRSを正規表現として保持します。
        // matrix-only Jointを雑に分解するとBind Poseを壊すため、Affine decomposition実装までは拒否します。
        if (node.Transform.HasExplicitTrs == false)
        {
            return SetError(
                errorMessage,
                context + " のJoint Node[" + std::to_string(nodeIndex) + "] がmatrix-onlyです");
        }

        if (node.ParentIndex == InvalidGltfIndex)
        {
            outRootJointNodes.emplace_back(nodeIndex);
            continue;
        }

        if (outNodeToJointSlot[node.ParentIndex] != InvalidGltfIndex)
        {
            continue;
        }

        // Joint集合外の親を持つNodeはSkeleton Rootとして扱います。
        // ただし現在のRaven SkinningはBone GlobalをMesh Bind Spaceとして扱うため、
        // その外側Transformが非Identityだと空間がずれます。Mesh Nodeとの空間変換を接続する
        // 次Phaseまでは、そのケースを黙って受理せず明示的に拒否します。
        if (node.ParentIndex >= globalTransforms.size()
            || IsNearlyIdentity(globalTransforms[node.ParentIndex]) == false)
        {
            return SetError(
                errorMessage,
                context + " のRoot Joint外側に非Identity Node Transformがあります");
        }

        outRootJointNodes.emplace_back(nodeIndex);
    }

    if (outRootJointNodes.empty())
    {
        return SetError(errorMessage, context + " にRoot Jointがありません");
    }

    return true;
}

bool BuildSkeleton(
    const NodeHierarchy& hierarchy,
    const std::vector<std::size_t>& jointNodes,
    const std::vector<std::size_t>& rootJointNodes,
    const std::vector<std::size_t>& nodeToJointSlot,
    const std::vector<math::Mat4>& inverseBindMatrices,
    ImportedSkin& outSkin,
    const std::string& context,
    std::string* errorMessage)
{
    const std::vector<Node>& nodes = hierarchy.GetNodes();

    outSkin.JointNodeIndices = jointNodes;
    outSkin.JointToBoneIndex.assign(jointNodes.size(), InvalidBoneIndex);
    outSkin.NodeToBoneIndex.assign(nodes.size(), InvalidBoneIndex);

    std::vector<bool> visited(nodes.size(), false);

    std::function<bool(std::size_t, BoneIndex)> appendJoint;
    appendJoint = [&](std::size_t nodeIndex, BoneIndex parentBone) -> bool
    {
        if (nodeIndex >= nodes.size())
        {
            return SetError(errorMessage, context + " Joint traversal indexが範囲外です");
        }
        if (visited[nodeIndex])
        {
            return SetError(errorMessage, context + " Joint traversalでNodeが重複しました");
        }

        const std::size_t jointSlot = nodeToJointSlot[nodeIndex];
        if (jointSlot == InvalidGltfIndex || jointSlot >= jointNodes.size())
        {
            return SetError(errorMessage, context + " Joint remapが不正です");
        }

        visited[nodeIndex] = true;
        const Node& node = nodes[nodeIndex];

        Bone bone{};
        bone.Name = node.Name.empty() ? "Joint_" + std::to_string(nodeIndex) : node.Name;
        bone.Parent = parentBone;
        bone.BindLocalTransform.Translation = node.Transform.Translation;
        bone.BindLocalTransform.Rotation = node.Transform.Rotation;
        bone.BindLocalTransform.Scale = node.Transform.Scale;

        BoneIndex boneIndex = InvalidBoneIndex;
        if (inverseBindMatrices.empty() == false)
        {
            boneIndex = outSkin.SkeletonData.AddBoneWithInverseBindMatrix(
                std::move(bone),
                inverseBindMatrices[jointSlot]);
        }
        else
        {
            boneIndex = outSkin.SkeletonData.AddBone(std::move(bone));
        }

        if (boneIndex == InvalidBoneIndex)
        {
            return SetError(errorMessage, context + " Skeleton::AddBone()に失敗しました");
        }

        outSkin.JointToBoneIndex[jointSlot] = boneIndex;
        outSkin.NodeToBoneIndex[nodeIndex] = boneIndex;

        for (std::size_t childNodeIndex : node.Children)
        {
            if (childNodeIndex >= nodeToJointSlot.size())
            {
                return SetError(errorMessage, context + " Child Node indexが範囲外です");
            }

            // JointではないScene NodeはSkeletonへ含めません。
            if (nodeToJointSlot[childNodeIndex] == InvalidGltfIndex)
            {
                continue;
            }

            if (appendJoint(childNodeIndex, boneIndex) == false)
            {
                return false;
            }
        }

        return true;
    };

    for (std::size_t rootJointNode : rootJointNodes)
    {
        if (appendJoint(rootJointNode, InvalidBoneIndex) == false)
        {
            return false;
        }
    }

    // joint集合内に「Joint -> 非Joint -> Joint」のような中間Nodeがある場合、
    // 上のdirect child traversalでは後段Jointへ到達しません。Bone Local Transformを正しく
    // 合成する処理が必要なので、現段階では未訪問Jointを検出して拒否します。
    for (std::size_t jointNode : jointNodes)
    {
        if (visited[jointNode] == false)
        {
            return SetError(
                errorMessage,
                context + " のJoint間に非Joint中間Nodeがあります（現段階では未対応）");
        }
    }

    return true;
}

bool ParseSkin(
    const JsonValue& skinValue,
    std::size_t skinIndex,
    const GltfDocument& document,
    const NodeHierarchy& hierarchy,
    const AccessorReader& accessorReader,
    const std::vector<math::Mat4>& globalTransforms,
    ImportedSkin& outSkin,
    std::string* errorMessage)
{
    const std::string context = "skins[" + std::to_string(skinIndex) + "]";
    if (skinValue.IsObject() == false)
    {
        return SetError(errorMessage, context + " はObjectである必要があります");
    }

    ImportedSkin imported;
    imported.SkinIndex = skinIndex;

    const JsonValue* name = skinValue.Find("name");
    if (name != nullptr)
    {
        if (name->IsString() == false)
        {
            return SetError(errorMessage, context + ".name はStringである必要があります");
        }
        imported.Name = name->GetString();
    }

    const std::size_t nodeCount = hierarchy.GetNodes().size();
    if (ParseJointIndices(
            skinValue,
            nodeCount,
            context,
            imported.JointNodeIndices,
            errorMessage) == false)
    {
        return false;
    }

    if (ParseOptionalSkinNodeIndex(
            skinValue,
            "skeleton",
            nodeCount,
            context,
            imported.SkeletonRootNodeIndex,
            errorMessage) == false)
    {
        return false;
    }

    std::size_t inverseBindAccessorIndex = InvalidGltfIndex;
    if (ParseOptionalAccessorIndex(
            skinValue,
            "inverseBindMatrices",
            document.GetAccessors().size(),
            context,
            inverseBindAccessorIndex,
            errorMessage) == false)
    {
        return false;
    }

    std::vector<math::Mat4> inverseBindMatrices;
    if (inverseBindAccessorIndex != InvalidGltfIndex)
    {
        if (accessorReader.ReadMat4(
                inverseBindAccessorIndex,
                inverseBindMatrices,
                errorMessage) == false)
        {
            return false;
        }
        if (inverseBindMatrices.size() != imported.JointNodeIndices.size())
        {
            return SetError(
                errorMessage,
                context + ".inverseBindMatrices countがjoints countと一致しません");
        }
    }

    std::vector<std::size_t> rootJointNodes;
    std::vector<std::size_t> nodeToJointSlot;
    if (ValidateJointHierarchy(
            hierarchy,
            imported.JointNodeIndices,
            globalTransforms,
            context,
            rootJointNodes,
            nodeToJointSlot,
            errorMessage) == false)
    {
        return false;
    }

    if (BuildSkeleton(
            hierarchy,
            imported.JointNodeIndices,
            rootJointNodes,
            nodeToJointSlot,
            inverseBindMatrices,
            imported,
            context,
            errorMessage) == false)
    {
        return false;
    }

    outSkin = std::move(imported);
    return true;
}

} // namespace

bool SkinImporter::LoadFromGlb(
    const std::string& filePath,
    std::vector<ImportedSkin>& outSkins,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    // Skin ImportではDocumentとNodeHierarchyの両方が必要です。
    // 同じGLBを二度開かないよう、Container/JSON Parseはここで一度だけ行い共有します。
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

    GltfDocument document;
    if (GltfDocument::BuildFromJson(
            root,
            std::move(glbData.BinaryChunk),
            document,
            errorMessage) == false)
    {
        return false;
    }

    NodeHierarchy hierarchy;
    if (NodeHierarchy::BuildFromJson(root, hierarchy, errorMessage) == false)
    {
        return false;
    }

    std::vector<math::Mat4> globalTransforms;
    if (hierarchy.BuildGlobalTransforms(globalTransforms, errorMessage) == false)
    {
        return false;
    }

    const JsonValue* skins = root.Find("skins");
    if (skins == nullptr)
    {
        outSkins.clear();
        return true;
    }
    if (skins->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.skinsはArrayである必要があります");
    }

    AccessorReader accessorReader(document);
    std::vector<ImportedSkin> importedSkins;
    const JsonValue::Array& array = skins->GetArray();
    importedSkins.reserve(array.size());

    for (std::size_t skinIndex = 0u; skinIndex < array.size(); ++skinIndex)
    {
        ImportedSkin imported;
        if (ParseSkin(
                array[skinIndex],
                skinIndex,
                document,
                hierarchy,
                accessorReader,
                globalTransforms,
                imported,
                errorMessage) == false)
        {
            return false;
        }

        importedSkins.emplace_back(std::move(imported));
    }

    outSkins = std::move(importedSkins);
    return true;
}

} // namespace Gltf
} // namespace Raven
