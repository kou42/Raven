#include "Raven/Data/Tests/CompiledTestAsset.h"
#include "Raven/Data/Formats/Binary/BinaryDocument.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include <iostream>
#include <filesystem>

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
    TestSettings settings;
    check(CompiledTestAsset::Load(valid, settings, error) && settings.Enabled
        && settings.Values == std::vector<int>({1, 2, 3}), "runtime round trip");
    check(CompiledTestAsset::Compile(source, compiled, error) && compiled == valid, "deterministic cache");
    std::string changed = source + " ";
    check(CompiledTestAsset::Compile(changed, compiled, error) && compiled != valid, "source cache invalidation");
    compiled = valid;
    compiled[4] = 3;
    settings.Label = "preserved";
    check(CompiledTestAsset::Load(compiled, settings, error) == false
        && settings.Label == "preserved", "reject version without mutation");
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
        graphLeaf = "leaf-v3";
        check(CompiledTestAsset::BuildCached(cachePath, source, {"root"}, graphResolver,
            graphEdges, cachedOutput, hit, error) && hit == false,
            "persistent cache dependency rebuild");
        std::filesystem::remove(cachePath, cacheIo);
        std::filesystem::remove(cacheDir, cacheIo);
    }
    else
    {
        check(false, "create cache test directory");
    }
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
