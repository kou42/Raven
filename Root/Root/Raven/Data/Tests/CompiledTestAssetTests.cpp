#include "Raven/Data/Tests/CompiledTestAsset.h"
#include "Raven/Data/Formats/Binary/BinaryDocument.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include <iostream>
#include <filesystem>
#include <fstream>

namespace Raven
{
namespace Data
{
namespace Tests
{
int RunCompiledTestAssetTests()
{
    int failures = 0;
    const std::string source = R"({"type":"Raven.StructuredDataTest","version":1,"enabled":true,"samples":[1,2,3]})";
    TestAssetError error;
    std::string compiled = "unchanged";
    auto check = [&](bool ok, const char* name)
    {
        if (ok == false) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
    };
    check(CompiledTestAsset::Compile(source, compiled, error), "compile v1 migration");
    const auto valid = compiled;
    // SHA-256既知値とHeader内のSource/空依存Manifest指紋を比較する。
    // 期待値は独立したSHA-256実装から取得した固定ベクトル。
    auto hexAt = [](std::string_view bytes, std::size_t offset, std::size_t count)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string hex;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto byte = static_cast<unsigned char>(bytes[offset + i]);
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 15u]);
        }
        return hex;
    };
    check(valid.size() >= 112
        && hexAt(valid, 16, 32) == "4c3c94f9a018bb5c209020e382f151450e23e0dba10fc9c75e92428a5d8ac78a",
        "sha256 known source vector");
    check(valid.size() >= 112
        && hexAt(valid, 80, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 empty dependency vector");
    TestSettings settings;
    check(CompiledTestAsset::Load(valid, settings, error) && settings.Enabled
        && settings.Values == std::vector<int>({1, 2, 3}), "runtime round trip");
    check(CompiledTestAsset::Compile(source, compiled, error) && compiled == valid, "deterministic cache");
    std::string changed = source + " ";
    check(CompiledTestAsset::Compile(changed, compiled, error) && compiled != valid, "source cache invalidation");
    compiled = valid;
    compiled[4] = 4;
    settings.Label = "preserved";
    check(CompiledTestAsset::Load(compiled, settings, error) == false
        && settings.Label == "preserved", "reject version without mutation");
    compiled = valid;
    compiled[16] ^= 1;
    check(CompiledTestAsset::IsCacheCurrent(compiled, source, {}, {}) == false,
        "reject damaged source digest");
    compiled = valid;
    compiled[48] ^= 1;
    check(CompiledTestAsset::Load(compiled, settings, error) == false,
        "reject damaged payload digest");
    compiled = valid;
    compiled[80] ^= 1;
    check(CompiledTestAsset::IsCacheCurrent(compiled, source, {}, {}) == false,
        "reject damaged dependency digest");
    compiled = valid;
    compiled[12] = static_cast<char>(255);
    check(CompiledTestAsset::Load(compiled, settings, error) == false, "reject invalid length");
    compiled = valid;
    compiled.back() ^= 1;
    check(CompiledTestAsset::Load(compiled, settings, error) == false, "reject checksum");
    check(CompiledTestAsset::Load(std::string_view(valid).substr(0, 3), settings, error) == false,
        "reject truncated header");
    // 依存Asset更新だけでもCache Missになり、解決失敗時は出力を保持する。
    std::string dependencyContent = "texture-v1";
    const CompiledTestAsset::DependencyResolver resolver =
        [&](std::string_view id, std::string& content)
        {
            if (id != "textures/test") { return false; }
            content = dependencyContent;
            return true;
        };
    std::string dependent;
    check(CompiledTestAsset::CompileWithDependencies(source, {"textures/test"}, resolver,
        dependent, error), "compile with dependency");
    check(CompiledTestAsset::IsCacheCurrent(dependent, source, {"textures/test"}, resolver),
        "cache hit with matching dependency");
    check(CompiledTestAsset::IsCacheCurrent(dependent, source + " ", {"textures/test"}, resolver) == false,
        "source edit invalidates cache");
    dependencyContent = "texture-v2";
    check(CompiledTestAsset::IsCacheCurrent(dependent, source, {"textures/test"}, resolver) == false,
        "dependency edit invalidates cache");
    compiled = valid;
    check(CompiledTestAsset::CompileWithDependencies(source, {"missing"}, resolver,
        compiled, error) == false && compiled == valid, "missing dependency preserves output");
    check(CompiledTestAsset::CompileWithDependencies(source, {"textures/test", "textures/test"},
        resolver, compiled, error) == false && compiled == valid, "duplicate dependency rejected");
    // ダイヤモンド依存を許容し、循環とEdge変更は検出する。
    std::string graphLeaf = "leaf-v1";
    bool cyclic = false;
    const CompiledTestAsset::DependencyResolver graphResolver =
        [&](std::string_view id, std::string& bytes)
        {
            if (id == "root" || id == "left" || id == "right")
            {
                bytes = "node";
                return true;
            }
            if (id == "leaf")
            {
                bytes = graphLeaf;
                return true;
            }
            return false;
        };
    const CompiledTestAsset::DependencyEnumerator graphEdges =
        [&](std::string_view id, std::vector<std::string>& children)
        {
            if (id == "root") { children = {"left", "right"}; }
            else if (id == "left" || id == "right") { children = {"leaf"}; }
            else if (id == "leaf" && cyclic) { children = {"root"}; }
            else { children.clear(); }
            return true;
        };
    std::string graphCompiled;
    check(CompiledTestAsset::CompileWithGraph(source, {"root"}, graphResolver, graphEdges,
        graphCompiled, error), "compile diamond graph");
    check(CompiledTestAsset::IsGraphCacheCurrent(graphCompiled, source, {"root"},
        graphResolver, graphEdges), "graph cache hit");
    // 到達ノードが同じでもRoot指定の違いは別のAsset入力として扱う。
    check(CompiledTestAsset::IsGraphCacheCurrent(graphCompiled, source, {"root", "left"},
        graphResolver, graphEdges) == false, "root set invalidates cache");
    graphLeaf = "leaf-v2";
    check(CompiledTestAsset::IsGraphCacheCurrent(graphCompiled, source, {"root"},
        graphResolver, graphEdges) == false, "transitive dependency invalidates cache");
    cyclic = true;
    check(CompiledTestAsset::CompileWithGraph(source, {"root"}, graphResolver, graphEdges,
        compiled, error) == false && compiled == valid, "dependency cycle rejected");
    cyclic = false;
    // 一時ディレクトリ上でMiss→Hit→依存更新Missを確認する。
    const auto cacheDir = std::filesystem::temp_directory_path()
        / ("raven-phase10-cache-" + std::to_string(
            static_cast<unsigned long long>(std::hash<std::string>{}(source))));
    std::error_code cacheIo;
    std::filesystem::create_directories(cacheDir, cacheIo);
    if (cacheIo.value() == 0)
    {
        const auto cachePath = cacheDir / "asset.rvca";
        std::filesystem::remove(cachePath, cacheIo);
        std::string cachedOutput;
        bool hit = false;
        check(CompiledTestAsset::BuildCached(cachePath, source, {"root"}, graphResolver,
            graphEdges, cachedOutput, hit, error) && hit == false, "persistent cache miss");
        check(CompiledTestAsset::BuildCached(cachePath, source, {"root"}, graphResolver,
            graphEdges, cachedOutput, hit, error) && hit, "persistent cache hit");
        // 破損CacheはHitさせず、再構築して復旧する。
        {
            std::ofstream corrupt(cachePath, std::ios::binary | std::ios::trunc);
            corrupt << "damaged";
        }
        check(CompiledTestAsset::BuildCached(cachePath, source, {"root"}, graphResolver,
            graphEdges, cachedOutput, hit, error) && hit == false,
            "corrupted persistent cache rebuild");
        check(CompiledTestAsset::BuildCached(cachePath, source, {"root"}, graphResolver,
            graphEdges, cachedOutput, hit, error) && hit,
            "repaired persistent cache hit");
        graphLeaf = "leaf-v3";
        check(CompiledTestAsset::BuildCached(cachePath, source, {"root"}, graphResolver,
            graphEdges, cachedOutput, hit, error) && hit == false,
            "persistent cache dependency rebuild");
        // 存在しない親Directoryへの保存失敗では出力も旧Cacheも変更しない。
        const auto invalidPath = cacheDir / "missing-parent" / "asset.rvca";
        const auto preserved = cachedOutput;
        hit = true;
        check(CompiledTestAsset::BuildCached(invalidPath, source, {"root"}, graphResolver,
            graphEdges, cachedOutput, hit, error) == false
            && hit == false && cachedOutput == preserved
            && std::filesystem::exists(cachePath),
            "cache write failure preserves output and old cache");
        std::filesystem::remove(cachePath, cacheIo);
        std::filesystem::remove(cacheDir, cacheIo);
    }
    else
    {
        check(false, "create cache test directory");
    }
    // ソース内の依存IDを自動抽出し、Graph Compilerへ接続する。
    const std::string withRefs = R"({"type":"Raven.StructuredDataTest","version":1,"enabled":true,"samples":[1,2,3],"dependencies":["root"]})";
    std::vector<std::string> extracted = {"keep"};
    check(CompiledTestAsset::ExtractRootDependencies(withRefs, extracted, error)
        && extracted == std::vector<std::string>({"root"}), "extract source dependencies");
    std::string autoCompiled;
    check(CompiledTestAsset::CompileFromSource(withRefs, graphResolver, graphEdges,
        autoCompiled, error), "compile extracted graph");
    check(CompiledTestAsset::ExtractRootDependencies(
        R"({"dependencies":["root","root"]})", extracted, error) == false
        && extracted == std::vector<std::string>({"root"}), "reject duplicate source dependencies");
    check(CompiledTestAsset::ExtractRootDependencies(
        R"({"dependencies":42})", extracted, error) == false
        && extracted == std::vector<std::string>({"root"}), "reject malformed dependency list");
    DataDocument document;
    DataError dataError;
    check(JsonParser::Parse(source, document, &dataError), "parse source document");
    std::string binary;
    check(BinaryDocument::Write(document, binary, &dataError), "write binary document");
    DataDocument decoded;
    check(BinaryDocument::Read(binary, decoded, &dataError), "read binary document");
    std::string damaged = binary;
    damaged[4] = 2;
    check(BinaryDocument::Read(damaged, decoded, &dataError) == false
        && decoded.GetRoot().IsValid(), "binary version preserves document");
    damaged = binary;
    damaged[12] = static_cast<char>(255);
    check(BinaryDocument::Read(damaged, decoded, &dataError) == false, "binary length rejection");
    std::cout << "Phase10: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}
#ifdef RAVEN_PHASE10_TEST_MAIN
int main() { return Raven::Data::Tests::RunCompiledTestAssetTests(); }
#endif
