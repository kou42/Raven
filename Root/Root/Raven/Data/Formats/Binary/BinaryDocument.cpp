#include "Raven/Data/Formats/Binary/BinaryDocument.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace Raven
{
namespace Data
{
namespace
{
constexpr std::size_t HeaderBytes = 20;
constexpr std::uint32_t EndianMarker = 0x01020304u;
void Put32(std::string& output, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        output.push_back(static_cast<char>((value >> (i * 8)) & 255u));
    }
}
std::uint32_t Get32(std::string_view bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (i * 8);
    }
    return value;
}
std::uint32_t Hash(std::string_view bytes)
{
    std::uint32_t value = 2166136261u;
    for (unsigned char byte : bytes)
    {
        value = (value ^ byte) * 16777619u;
    }
    return value;
}
bool Fail(DataError* error, DataErrorCode code, std::uint32_t offset)
{
    if (error != nullptr)
    {
        *error = {code, offset};
    }
    return false;
}
}
bool BinaryDocument::Write(const DataDocument& document, std::string& output, DataError* error)
{
    if (error != nullptr) { *error = {}; }
    try
    {
        std::string payload;
        if (MessagePackWriter::Write(document, payload, error) == false) { return false; }
        if (payload.size() > std::numeric_limits<std::uint32_t>::max() - HeaderBytes)
        {
            return Fail(error, DataErrorCode::LimitExceeded, 0);
        }
        std::string temporary;
        temporary.reserve(HeaderBytes + payload.size());
        temporary.append("RVBD", 4);
        Put32(temporary, Version);
        Put32(temporary, EndianMarker);
        Put32(temporary, static_cast<std::uint32_t>(payload.size()));
        Put32(temporary, Hash(payload));
        temporary.append(payload);
        output.swap(temporary);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return Fail(error, DataErrorCode::OutOfMemory, 0);
    }
    catch (const std::length_error&)
    {
        return Fail(error, DataErrorCode::LimitExceeded, 0);
    }
}
bool BinaryDocument::Read(std::string_view bytes, DataDocument& output, DataError* error,
    const DataLimits& limits)
{
    if (error != nullptr) { *error = {}; }
    if (bytes.size() < HeaderBytes)
    {
        return Fail(error, DataErrorCode::UnexpectedEnd, static_cast<std::uint32_t>(bytes.size()));
    }
    if (bytes.substr(0, 4) != "RVBD" || Get32(bytes, 4) != Version
        || Get32(bytes, 8) != EndianMarker)
    {
        return Fail(error, DataErrorCode::UnsupportedType, 0);
    }
    const std::uint32_t length = Get32(bytes, 12);
    // 減算による境界検査で、偽造Lengthの加算Overflowを防ぎます。
    if (length != bytes.size() - HeaderBytes || length > limits.MaxInputBytes)
    {
        return Fail(error, DataErrorCode::LimitExceeded, 12);
    }
    const auto payload = bytes.substr(HeaderBytes, length);
    if (Hash(payload) != Get32(bytes, 16))
    {
        return Fail(error, DataErrorCode::InvalidStructure, 16);
    }
    // Parserは一時Documentで全検証後だけCommitするため、失敗時のoutputを保持します。
    return MessagePackParser::Parse(payload, output, error, limits);
}
}
}
