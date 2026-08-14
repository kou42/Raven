// Raven/Gltf/GltfDocument.cpp
#include "Raven/Gltf/GltfDocument.h"

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

bool ReadSizeValue(const JsonValue& value, std::size_t& outValue)
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

bool ReadUint32Value(const JsonValue& value, std::uint32_t& outValue)
{
    std::size_t sizeValue = 0u;
    if (ReadSizeValue(value, sizeValue) == false)
    {
        return false;
    }
    if (sizeValue > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
    {
        return false;
    }

    outValue = static_cast<std::uint32_t>(sizeValue);
    return true;
}

bool ReadRequiredSize(
    const JsonValue& object,
    const char* key,
    std::size_t& outValue,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
    {
        return SetError(errorMessage, context + "." + key + " がありません");
    }
    if (ReadSizeValue(*value, outValue) == false)
    {
        return SetError(errorMessage, context + "." + key + " は0以上の整数である必要があります");
    }

    return true;
}

bool ReadOptionalSize(
    const JsonValue& object,
    const char* key,
    std::size_t& outValue,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
    {
        return true;
    }
    if (ReadSizeValue(*value, outValue) == false)
    {
        return SetError(errorMessage, context + "." + key + " は0以上の整数である必要があります");
    }

    return true;
}

bool ReadOptionalUint32(
    const JsonValue& object,
    const char* key,
    std::uint32_t& outValue,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
    {
        return true;
    }
    if (ReadUint32Value(*value, outValue) == false)
    {
        return SetError(errorMessage, context + "." + key + " はuint32範囲の整数である必要があります");
    }

    return true;
}

bool ReadOptionalBoolean(
    const JsonValue& object,
    const char* key,
    bool& outValue,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* value = object.Find(key);
    if (value == nullptr)
    {
        return true;
    }
    if (value->IsBoolean() == false)
    {
        return SetError(errorMessage, context + "." + key + " はBooleanである必要があります");
    }

    outValue = value->GetBoolean();
    return true;
}

bool ParseComponentType(std::uint32_t rawValue, ComponentType& outType)
{
    switch (rawValue)
    {
    case 5120u: outType = ComponentType::Byte; return true;
    case 5121u: outType = ComponentType::UnsignedByte; return true;
    case 5122u: outType = ComponentType::Short; return true;
    case 5123u: outType = ComponentType::UnsignedShort; return true;
    case 5125u: outType = ComponentType::UnsignedInt; return true;
    case 5126u: outType = ComponentType::Float; return true;
    default: return false;
    }
}

bool ParseAccessorType(const std::string& text, AccessorType& outType)
{
    if (text == "SCALAR")
    {
        outType = AccessorType::Scalar;
        return true;
    }
    if (text == "VEC2")
    {
        outType = AccessorType::Vec2;
        return true;
    }
    if (text == "VEC3")
    {
        outType = AccessorType::Vec3;
        return true;
    }
    if (text == "VEC4")
    {
        outType = AccessorType::Vec4;
        return true;
    }
    if (text == "MAT2")
    {
        outType = AccessorType::Mat2;
        return true;
    }
    if (text == "MAT3")
    {
        outType = AccessorType::Mat3;
        return true;
    }
    if (text == "MAT4")
    {
        outType = AccessorType::Mat4;
        return true;
    }

    return false;
}

std::size_t GetComponentByteSize(ComponentType type)
{
    switch (type)
    {
    case ComponentType::Byte:
    case ComponentType::UnsignedByte:
        return 1u;
    case ComponentType::Short:
    case ComponentType::UnsignedShort:
        return 2u;
    case ComponentType::UnsignedInt:
    case ComponentType::Float:
        return 4u;
    }

    return 0u;
}

std::size_t Align4(std::size_t value)
{
    return (value + 3u) & ~static_cast<std::size_t>(3u);
}

std::size_t GetElementByteSize(ComponentType componentType, AccessorType accessorType)
{
    const std::size_t componentSize = GetComponentByteSize(componentType);

    switch (accessorType)
    {
    case AccessorType::Scalar: return componentSize;
    case AccessorType::Vec2: return componentSize * 2u;
    case AccessorType::Vec3: return componentSize * 3u;
    case AccessorType::Vec4: return componentSize * 4u;

    // glTF Matrixは各Column先頭が4-byte境界へ揃う必要があります。
    // BYTE/SHORT系MAT2/MAT3では単純なcomponentCount * sizeにならない点が重要です。
    case AccessorType::Mat2: return Align4(componentSize * 2u) * 2u;
    case AccessorType::Mat3: return Align4(componentSize * 3u) * 3u;
    case AccessorType::Mat4: return Align4(componentSize * 4u) * 4u;
    }

    return 0u;
}

bool ParseAsset(const JsonValue& root, GltfDocument& document, std::string* errorMessage)
{
    const JsonValue* asset = root.Find("asset");
    if (asset == nullptr || asset->IsObject() == false)
    {
        return SetError(errorMessage, "glTF.asset Objectがありません");
    }

    const JsonValue* version = asset->Find("version");
    if (version == nullptr || version->IsString() == false)
    {
        return SetError(errorMessage, "glTF.asset.version Stringがありません");
    }

    if (version->GetString() != "2.0")
    {
        return SetError(errorMessage, "glTF 2.0以外は未対応です");
    }

    // private memberへ直接触れられないため、BuildFromJson側で再取得します。
    return true;
}

bool ParseBuffers(
    const JsonValue& root,
    std::vector<Buffer>& outBuffers,
    const std::vector<std::uint8_t>& binaryChunk,
    std::string* errorMessage)
{
    const JsonValue* buffers = root.Find("buffers");
    if (buffers == nullptr)
    {
        if (binaryChunk.empty() == false)
        {
            return SetError(errorMessage, "BIN ChunkがありますがglTF.buffersがありません");
        }
        return true;
    }
    if (buffers->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.buffersはArrayである必要があります");
    }

    const JsonValue::Array& array = buffers->GetArray();
    outBuffers.reserve(array.size());

    for (std::size_t i = 0u; i < array.size(); ++i)
    {
        const JsonValue& value = array[i];
        const std::string context = "buffers[" + std::to_string(i) + "]";
        if (value.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        Buffer buffer;
        if (ReadRequiredSize(value, "byteLength", buffer.ByteLength, context, errorMessage) == false)
        {
            return false;
        }

        const JsonValue* uri = value.Find("uri");
        if (uri != nullptr)
        {
            if (uri->IsString() == false)
            {
                return SetError(errorMessage, context + ".uri はStringである必要があります");
            }
            buffer.Uri = uri->GetString();
        }

        outBuffers.emplace_back(std::move(buffer));
    }

    // GLBのBIN ChunkはURIを持たない先頭Bufferへ対応します。
    if (binaryChunk.empty() == false)
    {
        if (outBuffers.empty())
        {
            return SetError(errorMessage, "BIN Chunkに対応するBufferがありません");
        }
        if (outBuffers[0].Uri.empty() == false)
        {
            return SetError(errorMessage, "GLB先頭Bufferがuriを持っています");
        }

        const std::size_t declaredSize = outBuffers[0].ByteLength;
        if (binaryChunk.size() < declaredSize)
        {
            return SetError(errorMessage, "BIN Chunkがbuffers[0].byteLengthより短いです");
        }
        if (binaryChunk.size() - declaredSize > 3u)
        {
            return SetError(errorMessage, "BIN ChunkのPaddingが3 byteを超えています");
        }
    }

    return true;
}

bool ParseBufferViews(
    const JsonValue& root,
    const std::vector<Buffer>& buffers,
    std::vector<BufferView>& outBufferViews,
    std::string* errorMessage)
{
    const JsonValue* bufferViews = root.Find("bufferViews");
    if (bufferViews == nullptr)
    {
        return true;
    }
    if (bufferViews->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.bufferViewsはArrayである必要があります");
    }

    const JsonValue::Array& array = bufferViews->GetArray();
    outBufferViews.reserve(array.size());

    for (std::size_t i = 0u; i < array.size(); ++i)
    {
        const JsonValue& value = array[i];
        const std::string context = "bufferViews[" + std::to_string(i) + "]";
        if (value.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        BufferView bufferView;
        if (ReadRequiredSize(value, "buffer", bufferView.BufferIndex, context, errorMessage) == false
            || ReadOptionalSize(value, "byteOffset", bufferView.ByteOffset, context, errorMessage) == false
            || ReadRequiredSize(value, "byteLength", bufferView.ByteLength, context, errorMessage) == false
            || ReadOptionalSize(value, "byteStride", bufferView.ByteStride, context, errorMessage) == false
            || ReadOptionalUint32(value, "target", bufferView.Target, context, errorMessage) == false)
        {
            return false;
        }

        if (bufferView.BufferIndex >= buffers.size())
        {
            return SetError(errorMessage, context + ".buffer が範囲外です");
        }
        if (bufferView.ByteStride != 0u
            && (bufferView.ByteStride < 4u || bufferView.ByteStride > 252u || (bufferView.ByteStride % 4u) != 0u))
        {
            return SetError(errorMessage, context + ".byteStride は4～252の4-byte倍数である必要があります");
        }
        if (bufferView.Target != 0u && bufferView.Target != 34962u && bufferView.Target != 34963u)
        {
            return SetError(errorMessage, context + ".target がglTF 2.0の許可値ではありません");
        }

        const Buffer& buffer = buffers[bufferView.BufferIndex];
        if (bufferView.ByteOffset > buffer.ByteLength
            || bufferView.ByteLength > buffer.ByteLength - bufferView.ByteOffset)
        {
            return SetError(errorMessage, context + " が参照Bufferの範囲を超えています");
        }

        outBufferViews.emplace_back(bufferView);
    }

    return true;
}

bool ParseAccessors(
    const JsonValue& root,
    const std::vector<BufferView>& bufferViews,
    std::vector<Accessor>& outAccessors,
    std::string* errorMessage)
{
    const JsonValue* accessors = root.Find("accessors");
    if (accessors == nullptr)
    {
        return true;
    }
    if (accessors->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.accessorsはArrayである必要があります");
    }

    const JsonValue::Array& array = accessors->GetArray();
    outAccessors.reserve(array.size());

    for (std::size_t i = 0u; i < array.size(); ++i)
    {
        const JsonValue& value = array[i];
        const std::string context = "accessors[" + std::to_string(i) + "]";
        if (value.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        // Sparse Accessorは後続Phaseで専用Readerを追加してから対応します。
        // 無視すると頂点位置やSkin Weightが静かに壊れるため、現段階では明示的に拒否します。
        if (value.Find("sparse") != nullptr)
        {
            return SetError(errorMessage, context + ".sparse は現段階では未対応です");
        }

        Accessor accessor;
        if (ReadOptionalSize(value, "byteOffset", accessor.ByteOffset, context, errorMessage) == false
            || ReadRequiredSize(value, "count", accessor.Count, context, errorMessage) == false
            || ReadOptionalBoolean(value, "normalized", accessor.Normalized, context, errorMessage) == false)
        {
            return false;
        }
        if (accessor.Count == 0u)
        {
            return SetError(errorMessage, context + ".count は1以上である必要があります");
        }

        const JsonValue* bufferViewValue = value.Find("bufferView");
        if (bufferViewValue != nullptr)
        {
            if (ReadSizeValue(*bufferViewValue, accessor.BufferViewIndex) == false)
            {
                return SetError(errorMessage, context + ".bufferView は0以上の整数である必要があります");
            }
            if (accessor.BufferViewIndex >= bufferViews.size())
            {
                return SetError(errorMessage, context + ".bufferView が範囲外です");
            }
        }

        const JsonValue* componentTypeValue = value.Find("componentType");
        if (componentTypeValue == nullptr)
        {
            return SetError(errorMessage, context + ".componentType がありません");
        }

        std::uint32_t rawComponentType = 0u;
        if (ReadUint32Value(*componentTypeValue, rawComponentType) == false
            || ParseComponentType(rawComponentType, accessor.Component) == false)
        {
            return SetError(errorMessage, context + ".componentType がglTF 2.0の許可値ではありません");
        }

        const JsonValue* typeValue = value.Find("type");
        if (typeValue == nullptr || typeValue->IsString() == false
            || ParseAccessorType(typeValue->GetString(), accessor.Type) == false)
        {
            return SetError(errorMessage, context + ".type がglTF 2.0の許可値ではありません");
        }

        if (accessor.BufferViewIndex != InvalidGltfIndex)
        {
            const BufferView& bufferView = bufferViews[accessor.BufferViewIndex];
            const std::size_t componentByteSize = GetComponentByteSize(accessor.Component);
            const std::size_t elementByteSize = GetElementByteSize(accessor.Component, accessor.Type);
            const std::size_t stride = bufferView.ByteStride == 0u ? elementByteSize : bufferView.ByteStride;

            if ((accessor.ByteOffset % componentByteSize) != 0u)
            {
                return SetError(errorMessage, context + ".byteOffset がComponent Size境界に揃っていません");
            }
            if (stride < elementByteSize)
            {
                return SetError(errorMessage, context + " のbyteStrideが要素サイズより小さいです");
            }
            if (accessor.ByteOffset > bufferView.ByteLength)
            {
                return SetError(errorMessage, context + ".byteOffset がBufferView範囲外です");
            }

            const std::size_t remaining = bufferView.ByteLength - accessor.ByteOffset;
            const std::size_t lastElementOffset = stride * (accessor.Count - 1u);

            // overflowを先に検査してから必要byte数を計算します。
            if (accessor.Count > 1u
                && stride > ((std::numeric_limits<std::size_t>::max)() / (accessor.Count - 1u)))
            {
                return SetError(errorMessage, context + " のAccessor範囲計算がoverflowしました");
            }
            if (lastElementOffset > remaining || elementByteSize > remaining - lastElementOffset)
            {
                return SetError(errorMessage, context + " がBufferView範囲を超えています");
            }
        }
        else if (accessor.ByteOffset != 0u)
        {
            return SetError(errorMessage, context + " はbufferView無しでbyteOffsetを指定できません");
        }

        outAccessors.emplace_back(accessor);
    }

    return true;
}

} // namespace

bool GltfDocument::LoadFromGlb(
    const std::string& filePath,
    GltfDocument& outDocument,
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

    return BuildFromJson(root, std::move(glbData.BinaryChunk), outDocument, errorMessage);
}

bool GltfDocument::BuildFromJson(
    const JsonValue& root,
    std::vector<std::uint8_t> binaryChunk,
    GltfDocument& outDocument,
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

    GltfDocument document;

    if (ParseAsset(root, document, errorMessage) == false)
    {
        return false;
    }

    const JsonValue* asset = root.Find("asset");
    const JsonValue* version = asset != nullptr ? asset->Find("version") : nullptr;
    if (version == nullptr)
    {
        return SetError(errorMessage, "glTF.asset.versionがありません");
    }
    document.m_AssetVersion = version->GetString();
    document.m_BinaryChunk = std::move(binaryChunk);

    if (ParseBuffers(root, document.m_Buffers, document.m_BinaryChunk, errorMessage) == false
        || ParseBufferViews(root, document.m_Buffers, document.m_BufferViews, errorMessage) == false
        || ParseAccessors(root, document.m_BufferViews, document.m_Accessors, errorMessage) == false)
    {
        return false;
    }

    outDocument = std::move(document);
    return true;
}

} // namespace Gltf
} // namespace Raven
