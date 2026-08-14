// Raven/Gltf/StaticMeshImporter.cpp
#include "Raven/Gltf/StaticMeshImporter.h"

#include <cmath>
#include <limits>
#include <utility>

#include "Raven/Gltf/AccessorReader.h"
#include "Raven/Gltf/GlbReader.h"
#include "Raven/Gltf/JsonParser.h"
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
    if (std::isfinite(number) == false || number < 0.0 || std::floor(number) != number)
    {
        return false;
    }

    // double -> size_t変換前に範囲を検査します。
    // 外部Assetから極端に大きい整数が来ても未定義/実装依存の整数変換へ進めません。
    if (number > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }

    outValue = static_cast<std::size_t>(number);
    return true;
}

bool FindAccessorIndex(
    const JsonValue& attributes,
    const char* semantic,
    std::size_t& outAccessorIndex,
    bool required,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* accessor = attributes.Find(semantic);
    if (accessor == nullptr)
    {
        if (required)
        {
            return SetError(errorMessage, context + ".attributes." + semantic + " がありません");
        }

        outAccessorIndex = InvalidGltfIndex;
        return true;
    }

    if (ReadSize(*accessor, outAccessorIndex) == false)
    {
        return SetError(errorMessage, context + ".attributes." + semantic + " が0以上の整数ではありません");
    }

    return true;
}

bool ValidatePositionAccessor(
    const GltfDocument& document,
    std::size_t accessorIndex,
    const std::string& context,
    std::string* errorMessage)
{
    const std::vector<Accessor>& accessors = document.GetAccessors();
    if (accessorIndex >= accessors.size())
    {
        return SetError(errorMessage, context + " POSITION accessor indexが範囲外です");
    }

    const Accessor& accessor = accessors[accessorIndex];
    if (accessor.Type != AccessorType::Vec3 || accessor.Component != ComponentType::Float)
    {
        return SetError(errorMessage, context + " POSITIONはFLOAT VEC3である必要があります");
    }
    if (accessor.Normalized)
    {
        return SetError(errorMessage, context + " POSITIONにnormalizedは使用できません");
    }

    return true;
}

bool ValidateNormalAccessor(
    const GltfDocument& document,
    std::size_t accessorIndex,
    const std::string& context,
    std::string* errorMessage)
{
    if (accessorIndex == InvalidGltfIndex)
    {
        return true;
    }

    const std::vector<Accessor>& accessors = document.GetAccessors();
    if (accessorIndex >= accessors.size())
    {
        return SetError(errorMessage, context + " NORMAL accessor indexが範囲外です");
    }

    const Accessor& accessor = accessors[accessorIndex];
    if (accessor.Type != AccessorType::Vec3)
    {
        return SetError(errorMessage, context + " NORMALはVEC3である必要があります");
    }

    if (accessor.Component == ComponentType::Float)
    {
        if (accessor.Normalized)
        {
            return SetError(errorMessage, context + " FLOAT NORMALにnormalizedは使用できません");
        }
        return true;
    }

    if ((accessor.Component == ComponentType::Byte || accessor.Component == ComponentType::Short)
        && accessor.Normalized)
    {
        return true;
    }

    return SetError(errorMessage, context + " NORMALのcomponentType/normalized組み合わせがglTF仕様外です");
}

bool ValidateTexCoordAccessor(
    const GltfDocument& document,
    std::size_t accessorIndex,
    const std::string& context,
    std::string* errorMessage)
{
    if (accessorIndex == InvalidGltfIndex)
    {
        return true;
    }

    const std::vector<Accessor>& accessors = document.GetAccessors();
    if (accessorIndex >= accessors.size())
    {
        return SetError(errorMessage, context + " TEXCOORD_0 accessor indexが範囲外です");
    }

    const Accessor& accessor = accessors[accessorIndex];
    if (accessor.Type != AccessorType::Vec2)
    {
        return SetError(errorMessage, context + " TEXCOORD_0はVEC2である必要があります");
    }

    if (accessor.Component == ComponentType::Float)
    {
        if (accessor.Normalized)
        {
            return SetError(errorMessage, context + " FLOAT TEXCOORD_0にnormalizedは使用できません");
        }
        return true;
    }

    if ((accessor.Component == ComponentType::UnsignedByte
            || accessor.Component == ComponentType::UnsignedShort)
        && accessor.Normalized)
    {
        return true;
    }

    return SetError(errorMessage, context + " TEXCOORD_0のcomponentType/normalized組み合わせがglTF仕様外です");
}

bool BuildPrimitiveGeometry(
    const JsonValue& primitive,
    const GltfDocument& document,
    const AccessorReader& accessorReader,
    const std::string& context,
    Ref<MeshGeometry>& outGeometry,
    std::size_t& outMaterialIndex,
    std::string* errorMessage)
{
    if (primitive.IsObject() == false)
    {
        return SetError(errorMessage, context + " はObjectである必要があります");
    }

    const JsonValue* modeValue = primitive.Find("mode");
    if (modeValue != nullptr)
    {
        std::size_t mode = 0u;
        if (ReadSize(*modeValue, mode) == false || mode != 4u)
        {
            return SetError(errorMessage, context + " はTRIANGLES(mode=4)以外未対応です");
        }
    }

    const JsonValue* attributes = primitive.Find("attributes");
    if (attributes == nullptr || attributes->IsObject() == false)
    {
        return SetError(errorMessage, context + ".attributes Objectがありません");
    }

    std::size_t positionAccessorIndex = InvalidGltfIndex;
    std::size_t normalAccessorIndex = InvalidGltfIndex;
    std::size_t texCoordAccessorIndex = InvalidGltfIndex;

    if (FindAccessorIndex(*attributes, "POSITION", positionAccessorIndex, true, context, errorMessage) == false
        || FindAccessorIndex(*attributes, "NORMAL", normalAccessorIndex, false, context, errorMessage) == false
        || FindAccessorIndex(*attributes, "TEXCOORD_0", texCoordAccessorIndex, false, context, errorMessage) == false)
    {
        return false;
    }

    if (ValidatePositionAccessor(document, positionAccessorIndex, context, errorMessage) == false
        || ValidateNormalAccessor(document, normalAccessorIndex, context, errorMessage) == false
        || ValidateTexCoordAccessor(document, texCoordAccessorIndex, context, errorMessage) == false)
    {
        return false;
    }

    std::vector<math::Vec3> positions;
    if (accessorReader.ReadVec3(positionAccessorIndex, positions, errorMessage) == false)
    {
        return false;
    }

    std::vector<math::Vec3> normals;
    if (normalAccessorIndex != InvalidGltfIndex)
    {
        if (accessorReader.ReadVec3(normalAccessorIndex, normals, errorMessage) == false)
        {
            return false;
        }
        if (normals.size() != positions.size())
        {
            return SetError(errorMessage, context + " NORMAL頂点数がPOSITIONと一致しません");
        }
    }

    std::vector<math::Vec2> texCoords;
    if (texCoordAccessorIndex != InvalidGltfIndex)
    {
        if (accessorReader.ReadVec2(texCoordAccessorIndex, texCoords, errorMessage) == false)
        {
            return false;
        }
        if (texCoords.size() != positions.size())
        {
            return SetError(errorMessage, context + " TEXCOORD_0頂点数がPOSITIONと一致しません");
        }
    }

    std::vector<std::uint32_t> indices;
    const JsonValue* indicesValue = primitive.Find("indices");
    if (indicesValue != nullptr)
    {
        std::size_t indexAccessorIndex = InvalidGltfIndex;
        if (ReadSize(*indicesValue, indexAccessorIndex) == false)
        {
            return SetError(errorMessage, context + ".indices が0以上の整数ではありません");
        }

        if (accessorReader.ReadIndices(indexAccessorIndex, indices, errorMessage) == false)
        {
            return false;
        }
    }
    else
    {
        if (positions.size() > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
        {
            return SetError(errorMessage, context + " の非Indexed頂点数がuint32範囲を超えています");
        }

        indices.resize(positions.size());
        for (std::size_t i = 0u; i < positions.size(); ++i)
        {
            indices[i] = static_cast<std::uint32_t>(i);
        }
    }

    if ((indices.size() % 3u) != 0u)
    {
        return SetError(errorMessage, context + " TRIANGLESのIndex数が3の倍数ではありません");
    }

    for (std::uint32_t index : indices)
    {
        if (static_cast<std::size_t>(index) >= positions.size())
        {
            return SetError(errorMessage, context + " のIndexがPOSITION頂点範囲外です");
        }
    }

    std::vector<MeshVertex> vertices;
    vertices.resize(positions.size());

    for (std::size_t i = 0u; i < positions.size(); ++i)
    {
        MeshVertex& vertex = vertices[i];
        vertex.Position = positions[i];

        if (normals.empty() == false)
        {
            vertex.Normal = normals[i];
        }

        if (texCoords.empty() == false)
        {
            vertex.TexCoord = texCoords[i];
        }
    }

    outMaterialIndex = InvalidGltfIndex;
    const JsonValue* materialValue = primitive.Find("material");
    if (materialValue != nullptr)
    {
        if (ReadSize(*materialValue, outMaterialIndex) == false)
        {
            return SetError(errorMessage, context + ".material が0以上の整数ではありません");
        }
    }

    outGeometry = CreateRef<MeshGeometry>(
        std::move(vertices),
        std::move(indices),
        GeometryUsage::Static,
        TopologyUsage::Fixed);

    return true;
}

} // namespace

bool StaticMeshImporter::LoadFromGlb(
    const std::string& filePath,
    std::vector<ImportedStaticPrimitive>& outPrimitives,
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

    GltfDocument document;
    if (GltfDocument::BuildFromJson(root, std::move(glbData.BinaryChunk), document, errorMessage) == false)
    {
        return false;
    }

    const JsonValue* meshes = root.Find("meshes");
    if (meshes == nullptr)
    {
        outPrimitives.clear();
        return true;
    }
    if (meshes->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.meshesはArrayである必要があります");
    }

    AccessorReader accessorReader(document);
    std::vector<ImportedStaticPrimitive> importedPrimitives;

    const JsonValue::Array& meshArray = meshes->GetArray();
    for (std::size_t meshIndex = 0u; meshIndex < meshArray.size(); ++meshIndex)
    {
        const JsonValue& mesh = meshArray[meshIndex];
        const std::string meshContext = "meshes[" + std::to_string(meshIndex) + "]";
        if (mesh.IsObject() == false)
        {
            return SetError(errorMessage, meshContext + " はObjectである必要があります");
        }

        std::string meshName;
        const JsonValue* nameValue = mesh.Find("name");
        if (nameValue != nullptr)
        {
            if (nameValue->IsString() == false)
            {
                return SetError(errorMessage, meshContext + ".name はStringである必要があります");
            }
            meshName = nameValue->GetString();
        }

        const JsonValue* primitives = mesh.Find("primitives");
        if (primitives == nullptr || primitives->IsArray() == false)
        {
            return SetError(errorMessage, meshContext + ".primitives Arrayがありません");
        }

        const JsonValue::Array& primitiveArray = primitives->GetArray();
        if (primitiveArray.empty())
        {
            return SetError(errorMessage, meshContext + ".primitives が空です");
        }

        for (std::size_t primitiveIndex = 0u; primitiveIndex < primitiveArray.size(); ++primitiveIndex)
        {
            ImportedStaticPrimitive imported;
            imported.MeshName = meshName;
            imported.MeshIndex = meshIndex;
            imported.PrimitiveIndex = primitiveIndex;

            const std::string primitiveContext = meshContext
                + ".primitives[" + std::to_string(primitiveIndex) + "]";

            if (BuildPrimitiveGeometry(
                    primitiveArray[primitiveIndex],
                    document,
                    accessorReader,
                    primitiveContext,
                    imported.Geometry,
                    imported.MaterialIndex,
                    errorMessage) == false)
            {
                return false;
            }

            importedPrimitives.emplace_back(std::move(imported));
        }
    }

    outPrimitives = std::move(importedPrimitives);
    return true;
}

} // namespace Gltf
} // namespace Raven
