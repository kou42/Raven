// Raven/Gltf/GlbReader.cpp
#include "Raven/Gltf/GlbReader.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

namespace Raven
{
namespace Gltf
{
namespace
{

constexpr std::uint32_t GlbMagic = 0x46546C67u;      // ASCII: glTF
constexpr std::uint32_t GlbVersion2 = 2u;
constexpr std::uint32_t JsonChunkType = 0x4E4F534Au; // ASCII: JSON
constexpr std::uint32_t BinChunkType = 0x004E4942u;  // ASCII: BIN\0
constexpr std::size_t HeaderSize = 12u;
constexpr std::size_t ChunkHeaderSize = 8u;

std::uint32_t ReadUint32LittleEndian(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

bool SetError(std::string* errorMessage, const char* message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

} // namespace

bool GlbReader::ReadFromFile(
    const std::string& filePath,
    GlbData& outData,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    std::ifstream file(filePath, std::ios::binary);
    if (file.is_open() == false)
    {
        return SetError(errorMessage, "GLBファイルを開けませんでした");
    }

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0)
    {
        return SetError(errorMessage, "GLBファイルサイズの取得に失敗しました");
    }
    if (static_cast<std::uint64_t>(fileSize) > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
    {
        return SetError(errorMessage, "GLBファイルが大きすぎます");
    }

    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
    if (bytes.empty() == false)
    {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (file.good() == false && file.eof() == false)
        {
            return SetError(errorMessage, "GLBファイルの読み込みに失敗しました");
        }
        if (static_cast<std::size_t>(file.gcount()) != bytes.size())
        {
            return SetError(errorMessage, "GLBファイルを最後まで読み込めませんでした");
        }
    }

    return Parse(bytes, outData, errorMessage);
}

bool GlbReader::Parse(
    const std::vector<std::uint8_t>& bytes,
    GlbData& outData,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    outData = GlbData{};

    if (bytes.size() < HeaderSize)
    {
        return SetError(errorMessage, "GLB Headerよりファイルが短いです");
    }

    const std::uint32_t magic = ReadUint32LittleEndian(bytes, 0u);
    const std::uint32_t version = ReadUint32LittleEndian(bytes, 4u);
    const std::uint32_t declaredLength = ReadUint32LittleEndian(bytes, 8u);

    if (magic != GlbMagic)
    {
        return SetError(errorMessage, "GLB magicがglTFではありません");
    }
    if (version != GlbVersion2)
    {
        return SetError(errorMessage, "glTF 2.0以外のGLBは未対応です");
    }
    if (static_cast<std::size_t>(declaredLength) != bytes.size())
    {
        return SetError(errorMessage, "GLB Headerのlengthと実ファイルサイズが一致しません");
    }

    std::size_t offset = HeaderSize;
    bool foundJsonChunk = false;
    bool foundBinChunk = false;
    std::size_t chunkIndex = 0u;

    while (offset < bytes.size())
    {
        if (bytes.size() - offset < ChunkHeaderSize)
        {
            return SetError(errorMessage, "GLB Chunk Headerが途中で切れています");
        }

        const std::uint32_t chunkLength = ReadUint32LittleEndian(bytes, offset);
        const std::uint32_t chunkType = ReadUint32LittleEndian(bytes, offset + 4u);
        offset += ChunkHeaderSize;

        if (static_cast<std::size_t>(chunkLength) > bytes.size() - offset)
        {
            return SetError(errorMessage, "GLB Chunk lengthがファイル範囲を超えています");
        }

        const std::size_t chunkBegin = offset;
        const std::size_t chunkEnd = offset + static_cast<std::size_t>(chunkLength);

        // glTF 2.0では最初のChunkが必ずJSONでなければなりません。
        if (chunkIndex == 0u && chunkType != JsonChunkType)
        {
            return SetError(errorMessage, "GLBの先頭ChunkがJSONではありません");
        }

        if (chunkType == JsonChunkType)
        {
            if (foundJsonChunk)
            {
                return SetError(errorMessage, "GLB内にJSON Chunkが複数あります");
            }

            outData.JsonText.assign(
                reinterpret_cast<const char*>(bytes.data() + chunkBegin),
                static_cast<std::size_t>(chunkLength));
            foundJsonChunk = true;
        }
        else if (chunkType == BinChunkType)
        {
            if (foundBinChunk)
            {
                return SetError(errorMessage, "GLB内にBIN Chunkが複数あります");
            }

            outData.BinaryChunk.assign(bytes.begin() + chunkBegin, bytes.begin() + chunkEnd);
            foundBinChunk = true;
        }
        else
        {
            // GLB仕様では未知Chunk Typeを無視できる必要があります。
            // 将来Extension独自Chunkが増えてもImporter全体を壊さないため、ここではSkipします。
        }

        offset = chunkEnd;
        ++chunkIndex;
    }

    if (foundJsonChunk == false)
    {
        return SetError(errorMessage, "GLBにJSON Chunkがありません");
    }

    return true;
}

} // namespace Gltf
} // namespace Raven
