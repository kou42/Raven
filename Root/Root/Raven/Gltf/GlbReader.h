// Raven/Gltf/GlbReader.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Raven
{
namespace Gltf
{

// ============================================================================
// GlbData
// ============================================================================
// GLB Containerから取り出した生のChunkデータです。
// JSONの意味解釈はGltfDocument側へ任せ、GlbReaderはContainer Formatだけを担当します。
struct GlbData
{
    std::string JsonText;
    std::vector<std::uint8_t> BinaryChunk;
};

// ============================================================================
// GlbReader
// ============================================================================
// glTF 2.0 Binary Container(.glb)を読み取ります。
//
// 責務:
// - 12-byte Header検証
// - version 2検証
// - JSON Chunk抽出
// - BIN Chunk抽出
// - 未知Chunkの安全なSkip
//
// Mesh / Node / Skin / Animationについては一切知りません。
class GlbReader
{
public:
    static bool ReadFromFile(
        const std::string& filePath,
        GlbData& outData,
        std::string* errorMessage = nullptr);

    // TestやMemory上へ読み込んだAssetにも利用できるよう、File I/OとContainer Parseを分離します。
    static bool Parse(
        const std::vector<std::uint8_t>& bytes,
        GlbData& outData,
        std::string* errorMessage = nullptr);
};

} // namespace Gltf
} // namespace Raven
