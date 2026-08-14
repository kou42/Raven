// Raven/Gltf/AccessorReader.cpp
#include "Raven/Gltf/AccessorReader.h"

#include <cstring>
#include <limits>

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

std::size_t GetComponentCount(AccessorType type)
{
    switch (type)
    {
    case AccessorType::Scalar: return 1u;
    case AccessorType::Vec2: return 2u;
    case AccessorType::Vec3: return 3u;
    case AccessorType::Vec4: return 4u;
    case AccessorType::Mat2: return 4u;
    case AccessorType::Mat3: return 9u;
    case AccessorType::Mat4: return 16u;
    }

    return 0u;
}

template<typename T>
T ReadUnaligned(const std::uint8_t* data)
{
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

float NormalizeSigned(std::int64_t value, std::int64_t positiveMax)
{
    const float normalized = static_cast<float>(value) / static_cast<float>(positiveMax);
    if (normalized < -1.0f)
    {
        return -1.0f;
    }

    return normalized;
}

float ReadFloatComponent(
    const std::uint8_t* data,
    ComponentType type,
    bool normalized)
{
    switch (type)
    {
    case ComponentType::Byte:
    {
        const std::int8_t value = ReadUnaligned<std::int8_t>(data);
        if (normalized)
        {
            return NormalizeSigned(value, 127);
        }
        return static_cast<float>(value);
    }
    case ComponentType::UnsignedByte:
    {
        const std::uint8_t value = ReadUnaligned<std::uint8_t>(data);
        if (normalized)
        {
            return static_cast<float>(value) / 255.0f;
        }
        return static_cast<float>(value);
    }
    case ComponentType::Short:
    {
        const std::int16_t value = ReadUnaligned<std::int16_t>(data);
        if (normalized)
        {
            return NormalizeSigned(value, 32767);
        }
        return static_cast<float>(value);
    }
    case ComponentType::UnsignedShort:
    {
        const std::uint16_t value = ReadUnaligned<std::uint16_t>(data);
        if (normalized)
        {
            return static_cast<float>(value) / 65535.0f;
        }
        return static_cast<float>(value);
    }
    case ComponentType::UnsignedInt:
    {
        const std::uint32_t value = ReadUnaligned<std::uint32_t>(data);
        if (normalized)
        {
            return static_cast<float>(value) / 4294967295.0f;
        }
        return static_cast<float>(value);
    }
    case ComponentType::Float:
        return ReadUnaligned<float>(data);
    }

    return 0.0f;
}

} // namespace

bool AccessorReader::ResolveAccessorBytes(
    std::size_t accessorIndex,
    const Accessor*& outAccessor,
    const BufferView*& outBufferView,
    const std::uint8_t*& outData,
    std::size_t& outStride,
    std::string* errorMessage) const
{
    const std::vector<Accessor>& accessors = m_Document.GetAccessors();
    const std::vector<BufferView>& bufferViews = m_Document.GetBufferViews();
    const std::vector<Buffer>& buffers = m_Document.GetBuffers();
    const std::vector<std::uint8_t>& binaryChunk = m_Document.GetBinaryChunk();

    if (accessorIndex >= accessors.size())
    {
        return SetError(errorMessage, "Accessor indexが範囲外です");
    }

    const Accessor& accessor = accessors[accessorIndex];
    if (accessor.BufferViewIndex == InvalidGltfIndex)
    {
        return SetError(errorMessage, "bufferViewを持たないAccessorは現段階では未対応です");
    }
    if (accessor.BufferViewIndex >= bufferViews.size())
    {
        return SetError(errorMessage, "Accessorが参照するBufferView indexが範囲外です");
    }

    const BufferView& bufferView = bufferViews[accessor.BufferViewIndex];
    if (bufferView.BufferIndex >= buffers.size())
    {
        return SetError(errorMessage, "BufferViewが参照するBuffer indexが範囲外です");
    }

    const Buffer& buffer = buffers[bufferView.BufferIndex];

    // 現在のGltfDocumentはGLBのBIN Chunkを保持しています。
    // URI付きBufferへ誤ってBIN Chunkを対応付けないよう明示的に拒否します。
    if (bufferView.BufferIndex != 0u || buffer.Uri.empty() == false)
    {
        return SetError(errorMessage, "外部URI Bufferは現段階では未対応です");
    }

    const std::size_t componentSize = GetComponentByteSize(accessor.Component);
    const std::size_t componentCount = GetComponentCount(accessor.Type);
    if (componentSize == 0u || componentCount == 0u)
    {
        return SetError(errorMessage, "AccessorのComponent情報が不正です");
    }

    // Vec系Accessorだけを対象にする低レベルReaderなのでMatrix alignmentはここでは不要です。
    // MatrixはinverseBindMatrices対応時に専用ReadMat4()を追加します。
    const std::size_t packedElementSize = componentSize * componentCount;
    const std::size_t stride = bufferView.ByteStride == 0u ? packedElementSize : bufferView.ByteStride;

    if (stride < packedElementSize)
    {
        return SetError(errorMessage, "Accessor strideが要素サイズより小さいです");
    }

    if (bufferView.ByteOffset > binaryChunk.size())
    {
        return SetError(errorMessage, "BufferView byteOffsetがBIN Chunk範囲外です");
    }
    if (accessor.ByteOffset > binaryChunk.size() - bufferView.ByteOffset)
    {
        return SetError(errorMessage, "Accessor byteOffsetがBIN Chunk範囲外です");
    }

    const std::size_t absoluteOffset = bufferView.ByteOffset + accessor.ByteOffset;
    if (absoluteOffset > binaryChunk.size())
    {
        return SetError(errorMessage, "Accessor絶対OffsetがBIN Chunk範囲外です");
    }

    outAccessor = &accessor;
    outBufferView = &bufferView;
    outData = binaryChunk.data() + absoluteOffset;
    outStride = stride;
    return true;
}

bool AccessorReader::ReadFloatComponents(
    std::size_t accessorIndex,
    AccessorType expectedType,
    std::size_t componentCount,
    std::vector<float>& outValues,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const Accessor* accessor = nullptr;
    const BufferView* bufferView = nullptr;
    const std::uint8_t* data = nullptr;
    std::size_t stride = 0u;
    if (ResolveAccessorBytes(
            accessorIndex,
            accessor,
            bufferView,
            data,
            stride,
            errorMessage) == false)
    {
        return false;
    }

    if (accessor == nullptr || bufferView == nullptr || data == nullptr)
    {
        return SetError(errorMessage, "Accessor解決結果が不正です");
    }
    if (accessor->Type != expectedType)
    {
        return SetError(errorMessage, "Accessor typeが要求されたVector型と一致しません");
    }

    if (accessor->Count > ((std::numeric_limits<std::size_t>::max)() / componentCount))
    {
        return SetError(errorMessage, "Accessor出力要素数の計算がoverflowしました");
    }

    outValues.clear();
    outValues.resize(accessor->Count * componentCount);

    const std::size_t componentSize = GetComponentByteSize(accessor->Component);
    for (std::size_t elementIndex = 0u; elementIndex < accessor->Count; ++elementIndex)
    {
        const std::uint8_t* elementData = data + stride * elementIndex;

        for (std::size_t componentIndex = 0u; componentIndex < componentCount; ++componentIndex)
        {
            outValues[elementIndex * componentCount + componentIndex] = ReadFloatComponent(
                elementData + componentSize * componentIndex,
                accessor->Component,
                accessor->Normalized);
        }
    }

    return true;
}

bool AccessorReader::ReadVec2(
    std::size_t accessorIndex,
    std::vector<math::Vec2>& outValues,
    std::string* errorMessage) const
{
    std::vector<float> values;
    if (ReadFloatComponents(accessorIndex, AccessorType::Vec2, 2u, values, errorMessage) == false)
    {
        return false;
    }

    outValues.clear();
    outValues.reserve(values.size() / 2u);
    for (std::size_t i = 0u; i < values.size(); i += 2u)
    {
        outValues.emplace_back(math::Vec2{ values[i], values[i + 1u] });
    }

    return true;
}

bool AccessorReader::ReadVec3(
    std::size_t accessorIndex,
    std::vector<math::Vec3>& outValues,
    std::string* errorMessage) const
{
    std::vector<float> values;
    if (ReadFloatComponents(accessorIndex, AccessorType::Vec3, 3u, values, errorMessage) == false)
    {
        return false;
    }

    outValues.clear();
    outValues.reserve(values.size() / 3u);
    for (std::size_t i = 0u; i < values.size(); i += 3u)
    {
        outValues.emplace_back(math::Vec3{ values[i], values[i + 1u], values[i + 2u] });
    }

    return true;
}

bool AccessorReader::ReadVec4(
    std::size_t accessorIndex,
    std::vector<math::Vec4>& outValues,
    std::string* errorMessage) const
{
    std::vector<float> values;
    if (ReadFloatComponents(accessorIndex, AccessorType::Vec4, 4u, values, errorMessage) == false)
    {
        return false;
    }

    outValues.clear();
    outValues.reserve(values.size() / 4u);
    for (std::size_t i = 0u; i < values.size(); i += 4u)
    {
        outValues.emplace_back(math::Vec4{ values[i], values[i + 1u], values[i + 2u], values[i + 3u] });
    }

    return true;
}

bool AccessorReader::ReadIndices(
    std::size_t accessorIndex,
    std::vector<std::uint32_t>& outIndices,
    std::string* errorMessage) const
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    const Accessor* accessor = nullptr;
    const BufferView* bufferView = nullptr;
    const std::uint8_t* data = nullptr;
    std::size_t stride = 0u;
    if (ResolveAccessorBytes(
            accessorIndex,
            accessor,
            bufferView,
            data,
            stride,
            errorMessage) == false)
    {
        return false;
    }

    if (accessor == nullptr || bufferView == nullptr || data == nullptr)
    {
        return SetError(errorMessage, "Index Accessor解決結果が不正です");
    }
    if (accessor->Type != AccessorType::Scalar)
    {
        return SetError(errorMessage, "Index AccessorはSCALARである必要があります");
    }
    if (accessor->Normalized)
    {
        return SetError(errorMessage, "Index Accessorにnormalizedは使用できません");
    }
    if (accessor->Component != ComponentType::UnsignedByte
        && accessor->Component != ComponentType::UnsignedShort
        && accessor->Component != ComponentType::UnsignedInt)
    {
        return SetError(errorMessage, "Index AccessorのcomponentTypeが未対応です");
    }

    outIndices.clear();
    outIndices.resize(accessor->Count);

    for (std::size_t i = 0u; i < accessor->Count; ++i)
    {
        const std::uint8_t* elementData = data + stride * i;

        if (accessor->Component == ComponentType::UnsignedByte)
        {
            outIndices[i] = static_cast<std::uint32_t>(ReadUnaligned<std::uint8_t>(elementData));
        }
        else if (accessor->Component == ComponentType::UnsignedShort)
        {
            outIndices[i] = static_cast<std::uint32_t>(ReadUnaligned<std::uint16_t>(elementData));
        }
        else
        {
            outIndices[i] = ReadUnaligned<std::uint32_t>(elementData);
        }
    }

    return true;
}

} // namespace Gltf
} // namespace Raven
