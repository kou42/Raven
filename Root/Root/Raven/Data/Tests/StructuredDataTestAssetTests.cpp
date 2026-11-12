#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include <fstream>
#include <iostream>
#include <iterator>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Raven
{
namespace Data
{
namespace Tests
{
namespace
{
int checks = 0;
int failures = 0;
void Check(bool success, const char* message)
{
    ++checks;
    if (success == false) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool Same(const TestSettings& a, const TestSettings& b)
{
    return a.Enabled == b.Enabled && a.Label == b.Label && a.Values == b.Values;
}
std::string ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void PutBytes(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    Check(file.fail() == false, "fixture write");
}
}
int RunTestAssetTests()
{
    checks = 0;
    failures = 0;
    // 専用に排他作成したディレクトリ内だけで実行します。利用者のAssetは変更しません。
    std::filesystem::path directory;
    for (unsigned i = 0; i < 128; ++i)
    {
        auto candidate = std::filesystem::temp_directory_path() / ("raven-data-asset-" + std::to_string(i));
        if (std::filesystem::create_directory(candidate)) { directory = candidate; break; }
    }
    Check(directory.empty() == false, "create isolated directory");
    if (directory.empty() == true) { return 1; }
    const auto path = directory / "settings.json";
    TestAssetError error;
    TestSettings settings{false, "preserved", {99}};
    const TestSettings original = settings;
    Check(LoadTestAsset(path, settings, error) == false && Same(settings, original), "missing file preserves caller defaults");
    Check(error.Stage == "input size", "missing file diagnostic");
    const auto legacy = std::filesystem::path("Root/Root/Raven/Data/Tests/StructuredDataTestAsset.v1.json");
    Check(LoadTestAsset(legacy, settings, error), "load checked-in v1 asset");
    const TestSettings expected{true, "test", {1, 2, 3}};
    Check(Same(settings, expected), "v1 samples migration");
    Check(SaveTestAsset(path, settings, error), "save new file");
    const auto saved = ReadBytes(path);
    Check(saved == "{\"enabled\":true,\"label\":\"test\",\"type\":\"Raven.StructuredDataTest\",\"values\":[1,2,3],\"version\":2}\n", "canonical v2 JSON contract");
    settings = original;
    Check(LoadTestAsset(path, settings, error) && Same(settings, expected), "disk round trip owns decoded data");
    Check(SaveTestAsset(path, settings, error) && ReadBytes(path) == saved, "existing file deterministic replacement");
    settings.Label = std::string(33, 'x');
    Check(SaveTestAsset(path, settings, error) == false && ReadBytes(path) == saved, "validation failure preserves original file");
    Check(error.Stage == "serialize" && std::string(error.Fields.Path.data()) == "/label", "save field diagnostic");
    settings = expected;
    settings.Values.resize(TestAssetMaxBytes, 12345);
    Check(SaveTestAsset(path, settings, error) == false && ReadBytes(path) == saved, "output limit preserves original file");
    Check(SaveTestAsset(directory / "missing" / "settings.json", expected, error) == false, "temporary creation failure");
#ifdef _WIN32
    // Delete共有なしで開き、実際のOS置換失敗を発生させます。
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock destination");
    if (locked != INVALID_HANDLE_VALUE)
    {
        Check(SaveTestAsset(path, original, error) == false && error.Stage == "replace", "locked destination rejects replacement");
        Check(ReadBytes(path) == saved, "OS replacement failure preserves original bytes");
        CloseHandle(locked);
    }
#endif
    Check(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator()) == 1,
        "temporary files cleaned after failures");
    const std::string prefix = "{\"type\":\"Raven.StructuredDataTest\",\"version\":2,";
    for (const std::string& bad : {
        std::string("{"),
        prefix + "\"enabled\":1}",
        prefix + "\"enabled\":true,\"label\":null}",
        prefix + "\"enabled\":true,\"values\":[1,\"bad\"]}",
        std::string("{\"type\":\"Raven.StructuredDataTest\",\"version\":3,\"enabled\":true}"),
        std::string(TestAssetMaxBytes + 1, ' ')})
    {
        PutBytes(path, bad);
        settings = original;
        Check(LoadTestAsset(path, settings, error) == false && Same(settings, original), "invalid file preserves complete setting");
        Check(error.Stage.empty() == false, "failure stage available");
    }
    PutBytes(path, prefix + "\"enabled\":true,\"values\":[1,\"bad\"]}");
    Check(LoadTestAsset(path, settings, error) == false && std::string(error.Fields.Path.data()) == "/values/1", "load field diagnostic");
    std::cout << "TestAsset diagnostic: stage=" << error.Stage << ", path=" << error.Fields.Path.data() << '\n';
    PutBytes(path, "{");
    Check(LoadTestAsset(path, settings, error) == false && error.Stage == "parse"
        && error.Parse.Code != DataErrorCode::None, "syntax diagnostic");
    PutBytes(path, prefix + "\"enabled\":true,\"future\":{\"x\":1}}");
    Check(LoadTestAsset(path, settings, error) && settings.Enabled && settings.Label.empty()
        && settings.Values.empty() && error.Stage.empty(), "optional defaults and unknown fields");
    PutBytes(path, prefix + "\"enabled\":true,\"label\":\"\\u65e5\\u672c\\n\"}");
    Check(LoadTestAsset(path, settings, error) && settings.Label == "\xe6\x97\xa5\xe6\x9c\xac\n", "escaped UTF-8 owns decoded text");
    Check(SaveTestAsset(path, settings, error), "save decoded text");
    const auto decoded = settings;
    Check(LoadTestAsset(path, settings, error) && Same(settings, decoded), "escaped text disk round trip");
    // 上限ちょうどを許可し、1超過だけを拒否することをファイル入力でも確認します。
    std::string boundary = prefix + "\"enabled\":true}";
    boundary.resize(TestAssetMaxBytes, ' ');
    PutBytes(path, boundary);
    Check(LoadTestAsset(path, settings, error), "exact input size limit");
    std::filesystem::remove(path);
    Check(std::filesystem::remove(directory), "isolated directory cleaned");
    std::cout << "TestAsset: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}
#ifdef RAVEN_TEST_ASSET_MAIN
int main() { return Raven::Data::Tests::RunTestAssetTests(); }
#endif
