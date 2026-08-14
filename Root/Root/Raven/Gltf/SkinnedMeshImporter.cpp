// Raven/Gltf/SkinnedMeshImporter.cpp
#include "Raven/Gltf/SkinnedMeshImporter.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Raven/Animation/SkinWeight.h"
#include "Raven/Gltf/AccessorReader.h"
#include "Raven/Gltf/GlbReader.h"
#include "Raven/Gltf/JsonParser.h"
#include "Raven/Gltf/NodeHierarchy.h"
#include "Raven/Gltf/StaticMeshImporter.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"

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

const JsonValue* GetPrimitiveValue(
    const JsonValue& root,
    std::size_t meshIndex,
    std::size_t primitiveIndex)
{
    const JsonValue* meshes = root.Find("meshes");
    if (meshes == nullptr || meshes->IsArray() == false)
    {
        return nullptr;
    }

    const JsonValue::Array& meshArray = meshes->GetArray();
    if (meshIndex >= meshArray.size())
    {
        return nullptr;
    }

    const JsonValue& mesh = meshArray[meshIndex];
    if (mesh.IsObject() == false)
    {
        return nullptr;
    }

    const JsonValue* primitives = mesh.Find("primitives");
    if (primitives == nullptr || primitives->IsArray() == false)
    {
        return nullptr;
    }

    const JsonValue::Array& primitiveArray = primitives->GetArray();
    if (primitiveIndex >= primitiveArray.size())
    {
        return nullptr;
    }

    return &primitiveArray[primitiveIndex];
}

bool ReadRequiredAttributeIndex(
    const JsonValue& attributes,
    const char* semantic,
    std::size_t& outAccessorIndex,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* value = attributes.Find(semantic);
    if (value == nullptr)
    {
        return SetError(errorMessage, context + ".attributes." + semantic + " がありません");
    }

    if (ReadSize(*value, outAccessorIndex) == false)
    {
        return SetError(
            errorMessage,
            context + ".attributes." + semantic + " が0以上の整数ではありません");
    }

    return true;
}

bool ValidateWeightAccessor(
    const GltfDocument& document,
    std::size_t accessorIndex,
    const std::string& context,
    std::string* errorMessage)
{
    const std::vector<Accessor>& accessors = document.GetAccessors();
    if (accessorIndex >= accessors.size())
    {
        return SetError(errorMessage, context + " WEIGHTS_0 Accessor indexが範囲外です");
    }

    const Accessor& accessor = accessors[accessorIndex];
    if (accessor.Type != AccessorType::Vec4)
    {
        return SetError(errorMessage, context + " WEIGHTS_0はVEC4である必要があります");
    }

    // FLOAT weightはそのまま値を持つためnormalized=falseでなければなりません。
    // 整数weightは[0,1]へ展開する必要があるためnormalized=trueだけを受理します。
    if (accessor.Component == ComponentType::Float)
    {
        if (accessor.Normalized)
        {
            return SetError(errorMessage, context + " FLOAT WEIGHTS_0にnormalizedは使用できません");
        }

        return true;
    }

    if ((accessor.Component == ComponentType::UnsignedByte
            || accessor.Component == ComponentType::UnsignedShort)
        && accessor.Normalized)
    {
        return true;
    }

    return SetError(
        errorMessage,
        context + " WEIGHTS_0のcomponentType/normalized組み合わせがglTF仕様外です");
}

bool BuildSkinWeights(
    const std::vector<AccessorReader::UnsignedVec4>& joints,
    const std::vector<math::Vec4>& weights,
    const ImportedSkin& skin,
    std::vector<SkinWeight>& outSkinWeights,
    const std::string& context,
    std::string* errorMessage)
{
    if (joints.size() != weights.size())
    {
        return SetError(errorMessage, context + " JOINTS_0とWEIGHTS_0の頂点数が一致しません");
    }

    outSkinWeights.clear();
    outSkinWeights.resize(joints.size());

    for (std::size_t vertexIndex = 0u; vertexIndex < joints.size(); ++vertexIndex)
    {
        const AccessorReader::UnsignedVec4& jointSlots = joints[vertexIndex];
        const math::Vec4& sourceWeights = weights[vertexIndex];
        const float weightValues[4] = {
            sourceWeights.x,
            sourceWeights.y,
            sourceWeights.z,
            sourceWeights.w
        };

        SkinWeight skinWeight{};

        for (std::size_t influenceIndex = 0u; influenceIndex < MaxBoneInfluences; ++influenceIndex)
        {
            const std::size_t jointSlot = static_cast<std::size_t>(jointSlots[influenceIndex]);

            // weight=0のslotであってもJOINTS_0はskin.joints配列を参照するindexです。
            // 不正indexを黙って無視するとAsset破損を見逃すため、全4slotを検証します。
            if (jointSlot >= skin.JointToBoneIndex.size())
            {
                return SetError(
                    errorMessage,
                    context + " vertex[" + std::to_string(vertexIndex)
                        + "] のJOINTS_0がskin.joints範囲外です");
            }

            const BoneIndex boneIndex = skin.JointToBoneIndex[jointSlot];
            if (skin.SkeletonData.IsValidBoneIndex(boneIndex) == false)
            {
                return SetError(
                    errorMessage,
                    context + " JOINTS_0から変換したBoneIndexがSkeleton範囲外です");
            }

            const float weight = weightValues[influenceIndex];
            if (std::isfinite(weight) == false || weight < 0.0f)
            {
                return SetError(
                    errorMessage,
                    context + " vertex[" + std::to_string(vertexIndex)
                        + "] のWEIGHTS_0に非有限値または負値があります");
            }

            // 0 WeightはInfluenceとして登録しません。
            // AddInfluence()は同一Boneが複数slotにある場合にWeightを統合してくれます。
            if (weight <= 0.0f)
            {
                continue;
            }

            if (skinWeight.AddInfluence(boneIndex, weight) == false)
            {
                return SetError(
                    errorMessage,
                    context + " vertex[" + std::to_string(vertexIndex)
                        + "] のSkinWeight構築に失敗しました");
            }
        }

        // Exporterの丸め誤差やnormalized整数展開後の誤差を吸収するため、
        // Import時点でWeight合計を1へ揃えます。全Weight=0は変形元が無いため拒否します。
        if (skinWeight.Normalize() == false)
        {
            return SetError(
                errorMessage,
                context + " vertex[" + std::to_string(vertexIndex)
                    + "] の有効Skin Weight合計が0です");
        }

        outSkinWeights[vertexIndex] = skinWeight;
    }

    return true;
}

const ImportedStaticPrimitive* FindStaticPrimitive(
    const std::vector<ImportedStaticPrimitive>& primitives,
    std::size_t meshIndex,
    std::size_t primitiveIndex)
{
    for (const ImportedStaticPrimitive& primitive : primitives)
    {
        if (primitive.MeshIndex == meshIndex && primitive.PrimitiveIndex == primitiveIndex)
        {
            return &primitive;
        }
    }

    return nullptr;
}

bool BuildSkinnedPrimitive(
    const JsonValue& root,
    const GltfDocument& document,
    const AccessorReader& accessorReader,
    const ImportedStaticPrimitive& staticPrimitive,
    const ImportedSkin& skin,
    std::size_t nodeIndex,
    const math::Mat4& worldTransform,
    ImportedSkinnedPrimitive& outPrimitive,
    std::string* errorMessage)
{
    const std::string context = "meshes[" + std::to_string(staticPrimitive.MeshIndex)
        + "].primitives[" + std::to_string(staticPrimitive.PrimitiveIndex) + "]";

    const JsonValue* primitiveValue = GetPrimitiveValue(
        root,
        staticPrimitive.MeshIndex,
        staticPrimitive.PrimitiveIndex);
    if (primitiveValue == nullptr || primitiveValue->IsObject() == false)
    {
        return SetError(errorMessage, context + " を再取得できませんでした");
    }

    const JsonValue* attributes = primitiveValue->Find("attributes");
    if (attributes == nullptr || attributes->IsObject() == false)
    {
        return SetError(errorMessage, context + ".attributes Objectがありません");
    }

    // Ravenの現SkinWeight上限は4 Influenceなので、追加4 Influenceを黙って捨てません。
    // JOINTS_1 / WEIGHTS_1対応時は上位4本の選択・再正規化方針を明示して拡張します。
    if (attributes->Find("JOINTS_1") != nullptr || attributes->Find("WEIGHTS_1") != nullptr)
    {
        return SetError(errorMessage, context + " JOINTS_1 / WEIGHTS_1は現段階では未対応です");
    }

    std::size_t jointsAccessorIndex = InvalidGltfIndex;
    std::size_t weightsAccessorIndex = InvalidGltfIndex;
    if (ReadRequiredAttributeIndex(
            *attributes,
            "JOINTS_0",
            jointsAccessorIndex,
            context,
            errorMessage) == false
        || ReadRequiredAttributeIndex(
            *attributes,
            "WEIGHTS_0",
            weightsAccessorIndex,
            context,
            errorMessage) == false)
    {
        return false;
    }

    if (ValidateWeightAccessor(document, weightsAccessorIndex, context, errorMessage) == false)
    {
        return false;
    }

    std::vector<AccessorReader::UnsignedVec4> joints;
    if (accessorReader.ReadUnsignedVec4(jointsAccessorIndex, joints, errorMessage) == false)
    {
        return false;
    }

    std::vector<math::Vec4> weights;
    if (accessorReader.ReadVec4(weightsAccessorIndex, weights, errorMessage) == false)
    {
        return false;
    }

    if (staticPrimitive.Geometry == nullptr)
    {
        return SetError(errorMessage, context + " Static MeshGeometryがありません");
    }

    const std::vector<MeshVertex>& sourceVertices = staticPrimitive.Geometry->GetVertices();
    if (joints.size() != sourceVertices.size() || weights.size() != sourceVertices.size())
    {
        return SetError(
            errorMessage,
            context + " POSITION / JOINTS_0 / WEIGHTS_0の頂点数が一致しません");
    }

    std::vector<SkinWeight> skinWeights;
    if (BuildSkinWeights(
            joints,
            weights,
            skin,
            skinWeights,
            context,
            errorMessage) == false)
    {
        return false;
    }

    std::vector<MeshVertex> dynamicVertices = sourceVertices;
    std::vector<std::uint32_t> indices = staticPrimitive.Geometry->GetIndices();

    std::vector<math::Vec3> bindPositions;
    bindPositions.reserve(dynamicVertices.size());
    for (const MeshVertex& vertex : dynamicVertices)
    {
        bindPositions.emplace_back(vertex.Position);
    }

    SkinnedMeshData skinningData(std::move(bindPositions), std::move(skinWeights));
    if (skinningData.Validate(skin.SkeletonData) == false)
    {
        return SetError(errorMessage, context + " 生成したSkinnedMeshDataの検証に失敗しました");
    }

    ImportedSkinnedPrimitive imported;
    imported.MeshName = staticPrimitive.MeshName;
    imported.NodeIndex = nodeIndex;
    imported.MeshIndex = staticPrimitive.MeshIndex;
    imported.PrimitiveIndex = staticPrimitive.PrimitiveIndex;
    imported.MaterialIndex = staticPrimitive.MaterialIndex;
    imported.SkinIndex = skin.SkinIndex;
    imported.WorldTransform = worldTransform;
    imported.Geometry = CreateRef<MeshGeometry>(
        std::move(dynamicVertices),
        std::move(indices),
        GeometryUsage::Dynamic,
        TopologyUsage::Fixed);
    imported.SkinningData = std::move(skinningData);

    outPrimitive = std::move(imported);
    return true;
}

} // namespace

bool SkinnedMeshImporter::LoadFromGlb(
    const std::string& filePath,
    ImportedSkinnedAsset& outAsset,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    // 既存のStaticMeshImporter / SkinImporterを正規変換経路として再利用します。
    // 現段階では各Importerが独立してGLBを開くためI/Oが重複しますが、責務を崩して処理を複製するより
    // まず正しい接続を優先します。Importer群が安定した段階でGltfAssetContextへ統合します。
    std::vector<ImportedStaticPrimitive> staticPrimitives;
    if (StaticMeshImporter::LoadFromGlb(filePath, staticPrimitives, errorMessage) == false)
    {
        return false;
    }

    std::vector<ImportedSkin> skins;
    if (SkinImporter::LoadFromGlb(filePath, skins, errorMessage) == false)
    {
        return false;
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

    const std::vector<Node>& nodes = hierarchy.GetNodes();
    AccessorReader accessorReader(document);
    ImportedSkinnedAsset importedAsset;
    importedAsset.Skins = std::move(skins);

    for (std::size_t nodeIndex = 0u; nodeIndex < nodes.size(); ++nodeIndex)
    {
        const Node& node = nodes[nodeIndex];

        // Static Mesh NodeはStaticSceneImporterの担当です。
        // skinとmeshの両方を持つNodeだけSkinned Primitiveとして生成します。
        if (node.MeshIndex == InvalidGltfIndex || node.SkinIndex == InvalidGltfIndex)
        {
            continue;
        }

        if (node.SkinIndex >= importedAsset.Skins.size())
        {
            return SetError(
                errorMessage,
                "nodes[" + std::to_string(nodeIndex) + "].skin がskins範囲外です");
        }
        if (nodeIndex >= globalTransforms.size())
        {
            return SetError(errorMessage, "Node Global Transform indexが範囲外です");
        }

        const ImportedSkin& skin = importedAsset.Skins[node.SkinIndex];
        bool foundPrimitive = false;

        for (const ImportedStaticPrimitive& staticPrimitive : staticPrimitives)
        {
            if (staticPrimitive.MeshIndex != node.MeshIndex)
            {
                continue;
            }

            foundPrimitive = true;
            ImportedSkinnedPrimitive importedPrimitive;
            if (BuildSkinnedPrimitive(
                    root,
                    document,
                    accessorReader,
                    staticPrimitive,
                    skin,
                    nodeIndex,
                    globalTransforms[nodeIndex],
                    importedPrimitive,
                    errorMessage) == false)
            {
                return false;
            }

            importedAsset.Primitives.emplace_back(std::move(importedPrimitive));
        }

        if (foundPrimitive == false)
        {
            return SetError(
                errorMessage,
                "nodes[" + std::to_string(nodeIndex) + "] が参照するMesh Primitiveを取得できませんでした");
        }
    }

    outAsset = std::move(importedAsset);
    return true;
}

} // namespace Gltf
} // namespace Raven
