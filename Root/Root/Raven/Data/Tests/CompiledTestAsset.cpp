#include "Raven/Data/Tests/CompiledTestAsset.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <limits>
#include <array>
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
constexpr std::size_t HeaderSize = 112;
constexpr std::uint32_t Marker = 0x01020304u;
using Digest = std::array<unsigned char, 32>;
std::uint32_t Rotate(std::uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}
// FIPS 180-4 SHA-256。外部ライブラリに依存せず、入力の複製を避ける。
Digest Sha256(std::string_view data)
{
    constexpr std::uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    std::uint32_t h[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    const std::size_t blocks = (data.size() + 9u + 63u) / 64u;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8u;
    for (std::size_t block = 0; block < blocks; ++block)
    {
        unsigned char bytes[64] = {};
        for (unsigned i = 0; i < 64; ++i)
        {
            const std::size_t pos = block * 64u + i;
            if (pos < data.size())
            {
                bytes[i] = static_cast<unsigned char>(data[pos]);
            }
            else if (pos == data.size())
            {
                bytes[i] = 0x80u;
            }
            if (pos >= blocks * 64u - 8u)
            {
                const unsigned shift = static_cast<unsigned>((blocks * 64u - 1u - pos) * 8u);
                bytes[i] = static_cast<unsigned char>(bitLength >> shift);
            }
        }
        std::uint32_t w[64] = {};
        for (unsigned i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<std::uint32_t>(bytes[4u*i]) << 24u)
                | (static_cast<std::uint32_t>(bytes[4u*i+1u]) << 16u)
                | (static_cast<std::uint32_t>(bytes[4u*i+2u]) << 8u)
                | static_cast<std::uint32_t>(bytes[4u*i+3u]);
        }
        for (unsigned i = 16; i < 64; ++i)
        {
            const auto a = Rotate(w[i-15],7) ^ Rotate(w[i-15],18) ^ (w[i-15] >> 3);
            const auto b = Rotate(w[i-2],17) ^ Rotate(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + a + w[i-7] + b;
        }
        auto a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], z=h[7];
        for (unsigned i = 0; i < 64; ++i)
        {
            const auto s1 = Rotate(e,6) ^ Rotate(e,11) ^ Rotate(e,25);
            const auto choose = (e & f) ^ (~e & g);
            const auto t1 = z + s1 + choose + k[i] + w[i];
            const auto s0 = Rotate(a,2) ^ Rotate(a,13) ^ Rotate(a,22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + majority;
            z=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=z;
    }
    Digest result{};
    for (unsigned i = 0; i < 8; ++i)
    {
        for (unsigned j = 0; j < 4; ++j)
        {
            result[i*4u+j] = static_cast<unsigned char>(h[i] >> (24u-8u*j));
        }
    }
    return result;
}
void PutDigest(std::string& bytes, const Digest& digest)
{
    bytes.append(reinterpret_cast<const char*>(digest.data()), digest.size());
}
bool MatchesDigest(std::string_view bytes, std::size_t offset, const Digest& digest)
{
    return bytes.size() >= offset + digest.size()
        && std::equal(digest.begin(), digest.end(), bytes.begin() + offset);
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
    if (roots.empty())
    {
        return true;
    }
    if (static_cast<bool>(resolver) == false || static_cast<bool>(enumerate) == false)
    {
        return false;
    }
    std::map<std::string, unsigned> states;
    std::function<bool(const std::string&)> visit = [&](const std::string& id)
    {
        if (id.empty() || id == std::string("\x01roots") || id.size() > 1024
            || (states.size() >= 255 && states.count(id) == 0))
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
    // Root集合も指紋に含める。既存Nodeの共有状態が同じでも
    // エントリポイントの変更がCache Hitにならないようにする。
    std::string rootRecord;
    Put(rootRecord, static_cast<std::uint32_t>(sortedRoots.size()));
    for (const auto& root : sortedRoots)
    {
        Put(rootRecord, static_cast<std::uint32_t>(root.size()));
        rootRecord.append(root);
    }
    contents.emplace(std::string("\x01roots"), std::move(rootRecord));
    ids.push_back(std::string("\x01roots"));
    return true;
}
Digest ResolveHash(const std::vector<std::string>& ids,
    const CompiledTestAsset::DependencyResolver& resolver, bool& success)
{
    success = false;
    if (ids.size() > 256 || (ids.empty() == false && static_cast<bool>(resolver) == false))
    {
        return {};
    }
    auto sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    std::string manifest;
    for (std::size_t i = 0; i < sorted.size(); ++i)
    {
        if (sorted[i].empty() || sorted[i].size() > 1024
            || (i > 0 && sorted[i] == sorted[i - 1]))
        {
            return {};
        }
        std::string content;
        if (resolver(sorted[i], content) == false
            || content.size() > (sorted[i] == std::string("\x01roots")
                ? 256u * 1028u + 4u : TestAssetMaxBytes))
        {
            return {};
        }
        // IDと内容を長さ付きで符号化し、境界の曖昧さを排除する。
        Put(manifest, static_cast<std::uint32_t>(sorted[i].size()));
        manifest.append(sorted[i]);
        Put(manifest, static_cast<std::uint32_t>(content.size()));
        manifest.append(content);
    }
    success = true;
    return Sha256(manifest);
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
        PutDigest(temporary, Sha256(sourceJson));
        PutDigest(temporary, Sha256(payload));
        PutDigest(temporary, dependencyHash);
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
bool CompiledTestAsset::ExtractRootDependencies(std::string_view sourceJson,
    std::vector<std::string>& output, TestAssetError& error)
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
        const auto root = document.GetRoot();
        if (root.IsType(DataType::Object) == false)
        {
            error.Stage = "dependencies root";
            return false;
        }
        DataElement field;
        if (root.TryFind("dependencies", field) == false)
        {
            std::vector<std::string> empty;
            output.swap(empty);
            return true;
        }
        if (field.IsType(DataType::Array) == false)
        {
            error.Stage = "dependencies type";
            return false;
        }
        std::uint32_t count = 0;
        if (field.TryGetCount(count) == false || count > 256)
        {
            error.Stage = "dependencies count";
            return false;
        }
        std::vector<std::string> ids;
        ids.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            DataElement element;
            std::string id;
            if (field.TryGetArrayElement(i, element) == false
                || element.TryGetDecodedString(id) == false
                || id.empty() || id.size() > 1024)
            {
                error.Stage = "dependencies entry";
                return false;
            }
            ids.push_back(std::move(id));
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
        {
            error.Stage = "dependencies duplicate";
            return false;
        }
        output.swap(ids);
        return true;
    }
    catch (const std::exception&)
    {
        error.Stage = "dependencies exception";
        return false;
    }
}
bool CompiledTestAsset::CompileFromSource(std::string_view sourceJson,
    const DependencyResolver& resolver, const DependencyEnumerator& enumerate,
    std::string& output, TestAssetError& error)
{
    std::vector<std::string> ids;
    if (ExtractRootDependencies(sourceJson, ids, error) == false)
    {
        return false;
    }
    return CompileWithGraph(sourceJson, ids, resolver, enumerate, output, error);
}
bool CompiledTestAsset::BuildCachedFromSource(const std::filesystem::path& cachePath,
    std::string_view sourceJson, const DependencyResolver& resolver,
    const DependencyEnumerator& enumerate, std::string& output, bool& cacheHit,
    TestAssetError& error)
{
    cacheHit = false;
    std::vector<std::string> ids;
    if (ExtractRootDependencies(sourceJson, ids, error) == false)
    {
        return false;
    }
    return BuildCached(cachePath, sourceJson, ids, resolver, enumerate, output, cacheHit, error);
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
    if (sourceJson.size() > TestAssetMaxBytes || compiled.size() < HeaderSize
        || compiled.size() > HeaderSize + TestAssetMaxBytes
        || compiled.substr(0, 4) != "RVCA"
        || Get(compiled, 4) != Version || Get(compiled, 8) != Marker
        || Get(compiled, 12) != compiled.size() - HeaderSize
        || MatchesDigest(compiled, 16, Sha256(sourceJson)) == false)
    {
        return false;
    }
    try
    {
        bool resolved = false;
        const auto dependencyHash = ResolveHash(dependencyIds, resolver, resolved);
        return resolved && MatchesDigest(compiled, 80, dependencyHash)
            && MatchesDigest(compiled, 48, Sha256(compiled.substr(HeaderSize)));
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
                // 指紋が一致してもPayloadが不正な型付きAssetなら再利用しない。
                TestSettings verified;
                TestAssetError validation;
                if (Load(cached, verified, validation))
                {
                    output.swap(cached);
                    cacheHit = true;
                    return true;
                }
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
    if (MatchesDigest(compiled, 48, Sha256(payload)) == false)
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
    // 失敗時に呼び出し側の設定値を変更しない。
    TestSettings decoded;
    if (Deserialize(document.GetRoot(), decoded, &error.Fields) == false)
    {
        error.Stage = "deserialize";
        return false;
    }
    output = std::move(decoded);
    return true;
}
}
}
}
