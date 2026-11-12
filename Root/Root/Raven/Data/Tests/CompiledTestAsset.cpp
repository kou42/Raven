#include "Raven/Data/Tests/CompiledTestAsset.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <limits>
#include <algorithm>
#include <exception>
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
constexpr std::size_t HeaderSize = 28;
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
std::uint32_t ResolveHash(const std::vector<std::string>& ids,
    const CompiledTestAsset::DependencyResolver& resolver, bool& success)
{
    success = false;
    if (ids.size() > 256 || (ids.empty() == false && static_cast<bool>(resolver) == false))
    {
        return 0;
    }
    auto sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    std::string manifest;
    for (std::size_t i = 0; i < sorted.size(); ++i)
    {
        if (sorted[i].empty() || sorted[i].size() > 1024
            || (i > 0 && sorted[i] == sorted[i - 1]))
        {
            return 0;
        }
        std::string content;
        if (resolver(sorted[i], content) == false || content.size() > TestAssetMaxBytes)
        {
            return 0;
        }
        // IDと内容を長さ付きで符号化し、境界の曖昧さを排除する。
        Put(manifest, static_cast<std::uint32_t>(sorted[i].size()));
        manifest.append(sorted[i]);
        Put(manifest, static_cast<std::uint32_t>(content.size()));
        manifest.append(content);
    }
    success = true;
    return Hash(manifest);
}
}
bool CompiledTestAsset::Compile(std::string_view sourceJson, std::string& output, TestAssetError& error)
{
    return CompileWithDependencies(sourceJson, {}, {}, output, error);
}
bool CompiledTestAsset::CompileWithDependencies(std::string_view sourceJson,
    const std::vector<std::string>& dependencyIds, const DependencyResolver& resolver,
    std::string& output, TestAssetError& error)
{
    error = {};
    try
    {
        if (sourceJson.size() > TestAssetMaxBytes)
        {
            error.Stage = "source size";
            return false;
        }
        bool resolved = false;
        const auto dependencyHash = ResolveHash(dependencyIds, resolver, resolved);
        if (resolved == false) { error.Stage = "dependency resolution"; return false; }
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
        Put(temporary, Hash(sourceJson) ^ (Version * 16777619u)); // Compiler Versionを含むCache識別子
        Put(temporary, Hash(payload));
        Put(temporary, dependencyHash);
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
bool CompiledTestAsset::IsCacheCurrent(std::string_view compiled, std::string_view sourceJson,
    const std::vector<std::string>& dependencyIds, const DependencyResolver& resolver)
{
    if (compiled.size() < HeaderSize || compiled.substr(0, 4) != "RVCA"
        || Get(compiled, 4) != Version || Get(compiled, 8) != Marker
        || Get(compiled, 12) != compiled.size() - HeaderSize
        || Get(compiled, 16) != (Hash(sourceJson) ^ (Version * 16777619u)))
    {
        return false;
    }
    try
    {
        bool resolved = false;
        const auto dependencyHash = ResolveHash(dependencyIds, resolver, resolved);
        return resolved && Get(compiled, 24) == dependencyHash
            && Hash(compiled.substr(HeaderSize)) == Get(compiled, 20);
    }
    catch (const std::exception&)
    {
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
