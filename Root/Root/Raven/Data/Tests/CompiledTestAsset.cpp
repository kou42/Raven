#include "Raven/Data/Tests/CompiledTestAsset.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <limits>
#include <algorithm>
#include <map>
#include <fstream>
#include <atomic>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
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
// グラフを一度だけ展開してSnapshot化する。解決結果の揺らぎを避けるため、
 // 後段のCompilerには同じSnapshotを渡す。
bool SnapshotGraph(const std::vector<std::string>& roots,
    const CompiledTestAsset::DependencyResolver& resolver,
    const CompiledTestAsset::DependencyEnumerator& enumerate,
    std::vector<std::string>& ids, std::map<std::string, std::string>& contents)
{
    if (static_cast<bool>(resolver) == false || static_cast<bool>(enumerate) == false)
    {
        return false;
    }
    std::map<std::string, unsigned> states;
    std::function<bool(const std::string&)> visit = [&](const std::string& id)
    {
        if (id.empty() || id.size() > 1024 || (states.size() >= 256 && states.count(id) == 0))
        {
            return false;
        }
        const auto found = states.find(id);
        if (found != states.end())
        {
            // 1は探索中のため循環。2は既に解決済みの共有依存。
            return found->second == 2;
        }
        states.emplace(id, 1);
        std::string bytes;
        std::vector<std::string> children;
        if (resolver(id, bytes) == false || bytes.size() > TestAssetMaxBytes
            || enumerate(id, children) == false || children.size() > 256)
        {
            return false;
        }
        std::sort(children.begin(), children.end());
        if (std::adjacent_find(children.begin(), children.end()) != children.end())
        {
            return false;
        }
        for (const auto& child : children)
        {
            if (visit(child) == false)
            {
                return false;
            }
        }
        // 依存先のIDも内容の一部として扱い、GraphのEdge変更をCacheへ反映する。
        std::string record;
        Put(record, static_cast<std::uint32_t>(children.size()));
        for (const auto& child : children)
        {
            Put(record, static_cast<std::uint32_t>(child.size()));
            record.append(child);
        }
        if (record.size() > TestAssetMaxBytes || bytes.size() > TestAssetMaxBytes - record.size())
        {
            return false;
        }
        record.append(bytes);
        contents.emplace(id, std::move(record));
        states[id] = 2;
        ids.push_back(id);
        return true;
    };
    auto sortedRoots = roots;
    std::sort(sortedRoots.begin(), sortedRoots.end());
    if (std::adjacent_find(sortedRoots.begin(), sortedRoots.end()) != sortedRoots.end())
    {
        return false;
    }
    for (const auto& root : sortedRoots)
    {
        if (visit(root) == false)
        {
            return false;
        }
    }
    return true;
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
bool CompiledTestAsset::CompileWithGraph(std::string_view sourceJson,
    const std::vector<std::string>& rootIds, const DependencyResolver& resolver,
    const DependencyEnumerator& enumerate, std::string& output, TestAssetError& error)
{
    error = {};
    try
    {
        std::vector<std::string> ids;
        std::map<std::string, std::string> contents;
        if (SnapshotGraph(rootIds, resolver, enumerate, ids, contents) == false)
        {
            error.Stage = "dependency graph";
            return false;
        }
        const DependencyResolver snapshot = [&](std::string_view id, std::string& bytes)
        {
            const auto found = contents.find(std::string(id));
            if (found == contents.end()) { return false; }
            bytes = found->second;
            return true;
        };
        return CompileWithDependencies(sourceJson, ids, snapshot, output, error);
    }
    catch (const std::exception&)
    {
        error.Stage = "dependency graph exception";
        return false;
    }
}
bool CompiledTestAsset::IsGraphCacheCurrent(std::string_view compiled, std::string_view sourceJson,
    const std::vector<std::string>& rootIds, const DependencyResolver& resolver,
    const DependencyEnumerator& enumerate)
{
    try
    {
        std::vector<std::string> ids;
        std::map<std::string, std::string> contents;
        if (SnapshotGraph(rootIds, resolver, enumerate, ids, contents) == false)
        {
            return false;
        }
        const DependencyResolver snapshot = [&](std::string_view id, std::string& bytes)
        {
            const auto found = contents.find(std::string(id));
            if (found == contents.end()) { return false; }
            bytes = found->second;
            return true;
        };
        return IsCacheCurrent(compiled, sourceJson, ids, snapshot);
    }
    catch (const std::exception&)
    {
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
bool CompiledTestAsset::BuildCached(const std::filesystem::path& cachePath,
    std::string_view sourceJson, const std::vector<std::string>& rootIds,
    const DependencyResolver& resolver, const DependencyEnumerator& enumerate,
    std::string& output, bool& cacheHit, TestAssetError& error)
{
    error = {};
    cacheHit = false;
    try
    {
        // Cacheは信用せず、Header・Checksum・依存Snapshotを毎回照合する。
        std::error_code io;
        const auto size = std::filesystem::file_size(cachePath, io);
        if (io.value() == 0 && size <= TestAssetMaxBytes + HeaderSize)
        {
            std::ifstream input(cachePath, std::ios::binary);
            std::string cached(static_cast<std::size_t>(size), '\0');
            input.read(cached.data(), static_cast<std::streamsize>(size));
            if (input.fail() == false && input.peek() == std::char_traits<char>::eof()
                && input.bad() == false
                && IsGraphCacheCurrent(cached, sourceJson, rootIds, resolver, enumerate))
            {
                output.swap(cached);
                cacheHit = true;
                return true;
            }
        }
        std::string rebuilt;
        if (CompileWithGraph(sourceJson, rootIds, resolver, enumerate, rebuilt, error) == false)
        {
            return false;
        }
        // 同じ親に排他的な一時Directoryを作り、完成後だけ旧Cacheを置換する。
        static std::atomic<unsigned long long> sequence{0};
        std::filesystem::path temporaryDir;
        for (unsigned attempt = 0; attempt < 128; ++attempt)
        {
            auto candidate = cachePath;
            candidate += ".pending-" + std::to_string(sequence.fetch_add(1));
            io.clear();
            if (std::filesystem::create_directory(candidate, io))
            {
                temporaryDir = std::move(candidate);
                break;
            }
            if (io && io != std::errc::file_exists) { break; }
        }
        if (temporaryDir.empty())
        {
            error.Stage = "cache temporary";
            return false;
        }
        struct Cleanup
        {
            std::filesystem::path Directory;
            ~Cleanup()
            {
                std::error_code ignored;
                std::filesystem::remove(Directory / "asset.rvca", ignored);
                std::filesystem::remove(Directory, ignored);
            }
        } cleanup{temporaryDir};
        const auto temporaryFile = temporaryDir / "asset.rvca";
        std::ofstream file(temporaryFile, std::ios::binary | std::ios::trunc);
        file.write(rebuilt.data(), static_cast<std::streamsize>(rebuilt.size()));
        file.flush();
        file.close();
        if (file.fail())
        {
            error.Stage = "cache write";
            return false;
        }
#ifdef _WIN32
        const bool replaced = MoveFileExW(temporaryFile.c_str(), cachePath.c_str(),
            MOVEFILE_REPLACE_EXISTING) != 0;
#else
        io.clear();
        std::filesystem::rename(temporaryFile, cachePath, io);
        const bool replaced = io.value() == 0;
#endif
        if (replaced == false)
        {
            error.Stage = "cache replace";
            return false;
        }
        output.swap(rebuilt);
        return true;
    }
    catch (const std::exception&)
    {
        error.Stage = "cache exception";
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
