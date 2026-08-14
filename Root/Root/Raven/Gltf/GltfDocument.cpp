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
    if (ReadSize(*value, outValue) == false)
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
    if (ReadSize(*value, outValue) == false)
    {
        return SetError(errorMessage, context + "." + key + " は0以上の整数である必要があります");
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

bool ReadUint32(const JsonValue& value, std::uint32_t& outValue)
{
    std::size_t sizeValue = 0u;
    if (ReadSize(value, sizeValue) == false)
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

bool ParseComponentType(std::uint32_t value, ComponentType& outType)
{
    switch (value)
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

bool ParseAccessorType(const std::string& value, AccessorType& outType)
{
    if (value == "SCALAR") { outType = AccessorType::Scalar; return true; }
    if (value == "VEC2") { outType = AccessorType::Vec2; return true; }
    if (value == "VEC3") { outType = AccessorType::Vec3; return true; }
    if (value == "VEC4") { outType = AccessorType::Vec4; return true; }
    if (value == "MAT2") { outType = AccessorType::Mat2; return true; }
    if (value == "MAT3") { outType = AccessorType::Mat3; return true; }
    if (value == "MAT4") { outType = AccessorType::Mat4; return true; }

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
    case AccessorType::Mat2: return Align4(componentSize * 2u) * 2u;
    case AccessorType::Mat3: return Align4(componentSize * 3u) * 3u;
    case AccessorType::Mat4: return Align4(componentSize * 4u) * 4u;
    }

    return 0u;
}

bool ParseBuffers(
    const JsonValue& root,
    const std::vector<std::uint8_t>& binaryChunk,
    std::vector<Buffer>& outBuffers,
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
    else if (outBuffers.empty() == false && outBuffers[0].Uri.empty() && outBuffers[0].ByteLength > 0u)
    {
        return SetError(errorMessage, "GLB先頭Bufferに対応するBIN Chunkがありません");
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

        BufferView view;
        if (ReadRequiredSize(value, "buffer", view.BufferIndex, context, errorMessage) == false
            || ReadOptionalSize(value, "byteOffset", view.ByteOffset, context, errorMessage) == false
            || ReadRequiredSize(value, "byteLength", view.ByteLength, context, errorMessage) == false
            || ReadOptionalSize(value, "byteStride", view.ByteStride, context, errorMessage) == false)
        {
            return false;
        }

        const JsonValue* target = value.Find("target");
        if (target != nullptr)
        {
            if (ReadUint32(*target, view.Target) == false)
            {
                return SetError(errorMessage, context + ".target はuint32範囲の整数である必要があります");
            }
        }

        if (view.BufferIndex >= buffers.size())
        {
            return SetError(errorMessage, context + ".buffer が範囲外です");
        }
        if (view.ByteStride != 0u
            && (view.ByteStride < 4u || view.ByteStride > 252u || (view.ByteStride % 4u) != 0u))
        {
            return SetError(errorMessage, context + ".byteStride は4～252の4-byte倍数である必要があります");
        }
        if (view.Target != 0u && view.Target != 34962u && view.Target != 34963u)
        {
            return SetError(errorMessage, context + ".target がglTF 2.0の許可値ではありません");
        }

        const Buffer& buffer = buffers[view.BufferIndex];
        if (view.ByteOffset > buffer.ByteLength || view.ByteLength > buffer.ByteLength - view.ByteOffset)
        {
            return SetError(errorMessage, context + " が参照Bufferの範囲を超えています");
        }

        outBufferViews.emplace_back(view);
    }

    return true;
}

bool ValidateAccessorRange(
    const Accessor& accessor,
    const BufferView& bufferView,
    const std::string& context,
    std::string* errorMessage)
{
    const std::size_t componentSize = GetComponentByteSize(accessor.Component);
    const std::size_t elementSize = GetElementByteSize(accessor.Component, accessor.Type);
    const std::size_t stride = bufferView.ByteStride == 0u ? elementSize : bufferView.ByteStride;

    if ((accessor.ByteOffset % componentSize) != 0u)
    {
        return SetError(errorMessage, context + ".byteOffset がComponent Size境界に揃っていません");
    }
    if (stride < elementSize)
    {
        return SetError(errorMessage, context + " のbyteStrideが要素サイズより小さいです");
    }
    if (accessor.ByteOffset > bufferView.ByteLength)
    {
        return SetError(errorMessage, context + ".byteOffset がBufferView範囲外です");
    }

    const std::size_t remaining = bufferView.ByteLength - accessor.ByteOffset;

    // 外部ファイル由来のcountを乗算する前にoverflowを検査します。
    // overflow後の値で範囲検証すると不正Accessorを通してしまうため、順序が重要です。
    std::size_t lastElementOffset = 0u;
    if (accessor.Count > 1u)
    {
        const std::size_t stepCount = accessor.Count - 1u;
        if (stride > ((std::numeric_limits<std::size_t>::max)() / stepCount))
        {
            return SetError(errorMessage, context + " のAccessor範囲計算がoverflowしました");
        }
        lastElementOffset = stride * stepCount;
    }

    if (lastElementOffset > remaining || elementSize > remaining - lastElementOffset)
    {
        return SetError(errorMessage, context + " がBufferView範囲を超えています");
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

        // Sparseを黙って無視するとPOSITIONやSkin Weightが静かに壊れるため、
        // 専用Readerを実装するまでは明示的に未対応として扱います。
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

        const JsonValue* bufferView = value.Find("bufferView");
        if (bufferView != nullptr)
        {
            if (ReadSize(*bufferView, accessor.BufferViewIndex) == false)
            {
                return SetError(errorMessage, context + ".bufferView は0以上の整数である必要があります");
            }
            if (accessor.BufferViewIndex >= bufferViews.size())
            {
                return SetError(errorMessage, context + ".bufferView が範囲外です");
            }
        }

        const JsonValue* componentType = value.Find("componentType");
        std::uint32_t rawComponentType = 0u;
        if (componentType == nullptr
            || ReadUint32(*componentType, rawComponentType) == false
            || ParseComponentType(rawComponentType, accessor.Component) == false)
        {
            return SetError(errorMessage, context + ".componentType がglTF 2.0の許可値ではありません");
        }

        const JsonValue* type = value.Find("type");
        if (type == nullptr || type->IsString() == false
            || ParseAccessorType(type->GetString(), accessor.Type) == false)
        {
            return SetError(errorMessage, context + ".type がglTF 2.0の許可値ではありません");
        }

        if (accessor.BufferViewIndex != InvalidGltfIndex)
        {
            if (ValidateAccessorRange(
                    accessor,
                    bufferViews[accessor.BufferViewIndex],
                    context,
                    errorMessage) == false)
            {
                return false;
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

    GltfDocument document;
    document.m_AssetVersion = version->GetString();
    document.m_BinaryChunk = std::move(binaryChunk);

    if (ParseBuffers(root, document.m_BinaryChunk, document.m_Buffers, errorMessage) == false
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
