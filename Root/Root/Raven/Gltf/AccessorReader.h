// Raven/Gltf/AccessorReader.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// AccessorReader
// ============================================================================
// glTF Accessor -> C++値への変換だけを担当するReaderです。
//
// Mesh / Skin / Animation側がBufferView / byteStride / componentType / normalized
// といった低レベル仕様を個別に理解し始めると、同じ処理が各Importerへ散らばります。
// そのためBinary Layoutの解決はこのクラスへ集約します。
//
// 現段階ではGLB内蔵BIN Chunkを対象にします。
// URI付き外部Bufferは後続の.gltf対応時にBufferResolverとして分離して追加します。
class AccessorReader
{
public:
    explicit AccessorReader(const GltfDocument& document)
        : m_Document(document)
    {
    }

    bool ReadVec2(
        std::size_t accessorIndex,
        std::vector<math::Vec2>& outValues,
        std::string* errorMessage = nullptr) const;

    bool ReadVec3(
        std::size_t accessorIndex,
        std::vector<math::Vec3>& outValues,
        std::string* errorMessage = nullptr) const;

    bool ReadVec4(
        std::size_t accessorIndex,
        std::vector<math::Vec4>& outValues,
        std::string* errorMessage = nullptr) const;

    // glTF indicesはUNSIGNED_BYTE / UNSIGNED_SHORT / UNSIGNED_INTのいずれかです。
    // Renderer側ではuint32_tへ統一して保持します。
    bool ReadIndices(
        std::size_t accessorIndex,
        std::vector<std::uint32_t>& outIndices,
        std::string* errorMessage = nullptr) const;

private:
    bool ReadFloatComponents(
        std::size_t accessorIndex,
        AccessorType expectedType,
        std::size_t componentCount,
        std::vector<float>& outValues,
        std::string* errorMessage) const;

    bool ResolveAccessorBytes(
        std::size_t accessorIndex,
        const Accessor*& outAccessor,
        const BufferView*& outBufferView,
        const std::uint8_t*& outData,
        std::size_t& outStride,
        std::string* errorMessage) const;

private:
    const GltfDocument& m_Document;
};

} // namespace Gltf
} // namespace Raven
