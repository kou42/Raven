// Raven/Gltf/GltfDocument.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Raven/Gltf/JsonValue.h"

namespace Raven
{
namespace Gltf
{

inline constexpr std::size_t InvalidGltfIndex = (std::numeric_limits<std::size_t>::max)();

// glTF Component Typeは仕様上の数値をそのまま保持します。
// 後続のAccessor Readerでbyte幅とC++型へ変換します。
enum class ComponentType : std::uint32_t
{
    Byte = 5120,
    UnsignedByte = 5121,
    Short = 5122,
    UnsignedShort = 5123,
    UnsignedInt = 5125,
    Float = 5126
};

enum class AccessorType
{
    Scalar,
    Vec2,
    Vec3,
    Vec4,
    Mat2,
    Mat3,
    Mat4
};

struct Buffer
{
    std::string Uri;
    std::size_t ByteLength = 0u;
};

struct BufferView
{
    std::size_t BufferIndex = InvalidGltfIndex;
    std::size_t ByteOffset = 0u;
    std::size_t ByteLength = 0u;
    std::size_t ByteStride = 0u;
    std::uint32_t Target = 0u;
};

struct Accessor
{
    std::size_t BufferViewIndex = InvalidGltfIndex;
    std::size_t ByteOffset = 0u;
    ComponentType Component = ComponentType::Float;
    std::size_t Count = 0u;
    AccessorType Type = AccessorType::Scalar;
    bool Normalized = false;
};

// ============================================================================
// GltfDocument
// ============================================================================
// JSON構文木をglTF 2.0の基礎データ構造へ変換したDocumentです。
//
// Phase 1ではAccessorからVertexを読むために必要な
// Buffer / BufferView / Accessorだけを扱います。
// Mesh / Node / Skin / Animationは後続PhaseでこのDocumentへ段階的に追加します。
class GltfDocument
{
public:
    // .glbをContainer Parse -> JSON Parse -> glTF Document変換まで一括で行います。
    static bool LoadFromGlb(
        const std::string& filePath,
        GltfDocument& outDocument,
        std::string* errorMessage = nullptr);

    // Testや将来.gltf対応でも再利用できるよう、JSON Valueからの構築を独立させます。
    static bool BuildFromJson(
        const JsonValue& root,
        std::vector<std::uint8_t> binaryChunk,
        GltfDocument& outDocument,
        std::string* errorMessage = nullptr);

    const std::string& GetAssetVersion() const { return m_AssetVersion; }
    const std::vector<Buffer>& GetBuffers() const { return m_Buffers; }
    const std::vector<BufferView>& GetBufferViews() const { return m_BufferViews; }
    const std::vector<Accessor>& GetAccessors() const { return m_Accessors; }
    const std::vector<std::uint8_t>& GetBinaryChunk() const { return m_BinaryChunk; }

private:
    std::string m_AssetVersion;
    std::vector<Buffer> m_Buffers;
    std::vector<BufferView> m_BufferViews;
    std::vector<Accessor> m_Accessors;
    std::vector<std::uint8_t> m_BinaryChunk;
};

} // namespace Gltf
} // namespace Raven
