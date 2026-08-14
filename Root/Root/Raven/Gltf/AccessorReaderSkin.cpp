// Raven/Gltf/AccessorReaderSkin.cpp
#include "Raven/Gltf/AccessorReader.h"

#include <cstring>

namespace Raven
{
namespace Gltf
{
namespace
{

bool SetSkinAccessorError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

template<typename T>
T ReadSkinAccessorUnaligned(const std::uint8_t* data)
{
    // JOINTS_0もbyteStride付きBufferView内へ配置される可能性があります。
    // 読み取り位置がuint16_tの自然alignmentに一致するとは限らないため、
    // reinterpret_castではなくmemcpyを使って未定義動作を避けます。
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

} // namespace

bool AccessorReader::ReadUnsignedVec4(
    std::size_t accessorIndex,
    std::vector<UnsignedVec4>& outValues,
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
        return SetSkinAccessorError(errorMessage, "Unsigned VEC4 Accessor解決結果が不正です");
    }

    // JOINTS_nはskin.joints配列のslot番号なので、normalizedしてはいけません。
    // またglTF 2.0ではUNSIGNED_BYTE / UNSIGNED_SHORT VEC4だけが許可されます。
    if (accessor->Type != AccessorType::Vec4)
    {
        return SetSkinAccessorError(errorMessage, "JOINTS_0 AccessorはVEC4である必要があります");
    }
    if (accessor->Normalized)
    {
        return SetSkinAccessorError(errorMessage, "JOINTS_0 Accessorにnormalizedは使用できません");
    }
    if (accessor->Component != ComponentType::UnsignedByte
        && accessor->Component != ComponentType::UnsignedShort)
    {
        return SetSkinAccessorError(
            errorMessage,
            "JOINTS_0 AccessorはUNSIGNED_BYTEまたはUNSIGNED_SHORTである必要があります");
    }

    const std::size_t componentSize =
        accessor->Component == ComponentType::UnsignedByte ? 1u : 2u;

    outValues.clear();
    outValues.resize(accessor->Count);

    for (std::size_t elementIndex = 0u; elementIndex < accessor->Count; ++elementIndex)
    {
        const std::uint8_t* elementData = data + stride * elementIndex;
        UnsignedVec4& value = outValues[elementIndex];

        for (std::size_t componentIndex = 0u; componentIndex < value.size(); ++componentIndex)
        {
            const std::uint8_t* componentData = elementData + componentSize * componentIndex;

            if (accessor->Component == ComponentType::UnsignedByte)
            {
                value[componentIndex] = static_cast<std::uint32_t>(
                    ReadSkinAccessorUnaligned<std::uint8_t>(componentData));
            }
            else
            {
                value[componentIndex] = static_cast<std::uint32_t>(
                    ReadSkinAccessorUnaligned<std::uint16_t>(componentData));
            }
        }
    }

    return true;
}

} // namespace Gltf
} // namespace Raven
