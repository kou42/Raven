#include "Raven/Data/Tests/CompiledTestAsset.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <limits>
#include <new>
#include <stdexcept>

namespace Raven
{
namespace Data
{
namespace Tests
{
namespace
{
constexpr std::size_t HeaderSize = 24;
constexpr std::uint32_t Marker = 0x01020304u;
std::uint32_t Hash(std::string_view data)
{
    std::uint32_t hash = 2166136261u;
    for (unsigned char byte : data)
    {
        hash = (hash ^ byte) * 16777619u;
    }
    return hash;
}
void Put(std::string& bytes, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        bytes.push_back(static_cast<char>((value >> (i * 8)) & 255u));
    }
}
std::uint32_t Get(std::string_view bytes, std::size_t offset)
{
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    }
    return result;
}
}
bool CompiledTestAsset::Compile(std::string_view sourceJson, std::string& output, TestAssetError& error)
{
    error = {};
    try
    {
        if (sourceJson.size() > TestAssetMaxBytes)
        {
            error.Stage = "source size";
            return false;
        }
        DataLimits limits;
        limits.MaxInputBytes = TestAssetMaxBytes;
        DataDocument document;
        if (JsonParser::Parse(sourceJson, document, &error.Parse, limits) == false)
        {
            error.Stage = "parse";
            return false;
        }
        TestSettings settings;
        if (Deserialize(document.GetRoot(), settings, &error.Fields) == false)
        {
            error.Stage = "deserialize";
            return false;
        }
        // Migration後の型付き値だけをRuntimeへ書き込み、未知フィールドを持ち込まない。
        DataWriteLimits writeLimits;
        writeLimits.MaxOutputBytes = TestAssetMaxBytes;
        MessagePackWriter writer(writeLimits);
        std::string payload;
        if (Serialize(writer, settings, &error.Fields) == false || writer.Finish(payload) == false)
        {
            error.Stage = "serialize";
            error.Parse = writer.GetError();
            return false;
        }
        std::string temporary;
        temporary.reserve(HeaderSize + payload.size());
        temporary.append("RVCA", 4);
        Put(temporary, Version);
        Put(temporary, Marker);
        Put(temporary, static_cast<std::uint32_t>(payload.size()));
        Put(temporary, Hash(sourceJson)); // Cache Key: 原文変更で再Compile
        Put(temporary, Hash(payload));
        temporary.append(payload);
        output.swap(temporary);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        error.Stage = "out of memory";
        return false;
    }
    catch (const std::length_error&)
    {
        error.Stage = "size limit";
        return false;
    }
}
bool CompiledTestAsset::Load(std::string_view compiled, TestSettings& output, TestAssetError& error)
{
    error = {};
    if (compiled.size() < HeaderSize)
    {
        error.Stage = "header truncated";
        return false;
    }
    if (compiled.substr(0, 4) != "RVCA" || Get(compiled, 4) != Version
        || Get(compiled, 8) != Marker)
    {
        error.Stage = "header version";
        return false;
    }
    const auto size = Get(compiled, 12);
    if (size > TestAssetMaxBytes || size != compiled.size() - HeaderSize)
    {
        error.Stage = "payload size";
        return false;
    }
    const auto payload = compiled.substr(HeaderSize, size);
    if (Hash(payload) != Get(compiled, 20))
    {
        error.Stage = "payload checksum";
        return false;
    }
    DataLimits limits;
    limits.MaxInputBytes = TestAssetMaxBytes;
    DataDocument document;
    if (MessagePackParser::Parse(payload, document, &error.Parse, limits) == false)
    {
        error.Stage = "parse";
        return false;
    }
    if (Deserialize(document.GetRoot(), output, &error.Fields) == false)
    {
        error.Stage = "deserialize";
        return false;
    }
    return true;
}
}
}
}
