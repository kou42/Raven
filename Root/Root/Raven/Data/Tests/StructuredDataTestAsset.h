#pragma once
#include "Raven/Data/Serialization/Serialization.h"
#include <filesystem>

namespace Raven
{
namespace Data
{
namespace Tests
{
// 専用テストAsset。実運用の設定形式とは独立し、v1のsamplesをv2のvaluesへ移行します。
struct TestSettings
{
    bool Enabled = false;
    std::string Label;
    std::vector<int> Values;
};
}
template<> struct DataAdapter<Tests::TestSettings>
{
    static bool Read(DataElement input, Tests::TestSettings& output, SerializationContext& context);
    static bool Write(DataWriter& writer, const Tests::TestSettings& value, SerializationContext& context);
};
namespace Tests
{
// I/O診断と構文・フィールド診断を分けて保持します。失敗時に設定を既定値で上書きしません。
struct TestAssetError
{
    std::string Stage;
    DataError Parse;
    SerializationError Fields;
};
inline constexpr std::uint32_t TestAssetMaxBytes = 64u * 1024u;
bool LoadTestAsset(const std::filesystem::path& path, TestSettings& output, TestAssetError& error);
bool SaveTestAsset(const std::filesystem::path& path, const TestSettings& value, TestAssetError& error);
int RunTestAssetTests();
}
}
}
