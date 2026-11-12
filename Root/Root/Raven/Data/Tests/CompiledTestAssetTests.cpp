#include "Raven/Data/Tests/CompiledTestAsset.h"
#include "Raven/Data/Formats/Binary/BinaryDocument.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include <iostream>

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
    compiled[4] = 2;
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
