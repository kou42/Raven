#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include "Raven/Data/DataDocument.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/Json/JsonWriter.h"
#include <fstream>
#include <atomic>
#include <exception>
#include <utility>
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
bool DataAdapter<Tests::TestSettings>::Read(DataElement input, Tests::TestSettings& output, SerializationContext& context)
{
    ObjectReader object(input, context);
    std::uint64_t version = 0;
    if (object.Header("Raven.StructuredDataTest", 1, 2, version) == false
        || object.Required("enabled", output.Enabled) == false
        || object.Optional("label", output.Label) == false
        || object.Optional(version == 1 ? "samples" : "values", output.Values) == false) { return false; }
    // Migrationは一時値の中で完了します。型変換後の業務検証もCommit前に行います。
    if (output.Label.size() > 32)
    {
        auto field = context.Field("label");
        return field.Fail(SerializationErrorCode::ValidationFailed, input);
    }
    return true;
}
bool DataAdapter<Tests::TestSettings>::Write(DataWriter& writer, const Tests::TestSettings& value, SerializationContext& context)
{
    if (value.Label.size() > 32)
    {
        auto field = context.Field("label");
        return field.Fail(SerializationErrorCode::ValidationFailed);
    }
    return context.Written(writer.BeginObject(), writer)
        && WriteField(writer, "type", std::string("Raven.StructuredDataTest"), context)
        && WriteField(writer, "version", 2u, context)
        && WriteField(writer, "enabled", value.Enabled, context)
        && WriteField(writer, "label", value.Label, context)
        && WriteField(writer, "values", value.Values, context)
        && context.Written(writer.EndObject(), writer);
}
namespace Tests
{
namespace
{
// 同じ親ディレクトリに排他的に作成します。既存の一時ファイルを切り詰めません。
// 所有しているファイルと空ディレクトリだけを削除し、再帰削除は行いません。
struct TemporaryAsset
{
    std::filesystem::path Directory;
    std::filesystem::path File;
    ~TemporaryAsset()
    {
        std::error_code ignored;
        if (Directory.empty() == false)
        {
            std::filesystem::remove(File, ignored);
            std::filesystem::remove(Directory, ignored);
        }
    }
};
}
bool LoadTestAsset(const std::filesystem::path& path, TestSettings& output, TestAssetError& error)
{
    error = {};
    try
    {
        std::error_code io;
        const auto size = std::filesystem::file_size(path, io);
        if (io || size > TestAssetMaxBytes)
        {
            error.Stage = "input size";
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        file.read(bytes.data(), static_cast<std::streamsize>(size));
        // サイズ検査後の短いReadと追記も拒否し、不完全な設定を公開しません。
        if (file.fail() || file.peek() != std::char_traits<char>::eof() || file.bad())
        {
            error.Stage = "read";
            return false;
        }
        DataLimits limits;
        limits.MaxInputBytes = TestAssetMaxBytes;
        DataDocument document;
        if (JsonParser::Parse(bytes, document, &error.Parse, limits) == false)
        {
            error.Stage = "parse";
            return false;
        }
        if (Deserialize(document.GetRoot(), output, &error.Fields) == false)
        {
            error.Stage = "deserialize";
            return false;
        }
        // outputは所有String / Vectorだけを保持し、Document破棄後も利用できます。
        return true;
    }
    catch (const std::exception&)
    {
        error.Stage = "read exception";
        return false;
    }
}
bool SaveTestAsset(const std::filesystem::path& path, const TestSettings& value, TestAssetError& error)
{
    error = {};
    TemporaryAsset temporary;
    try
    {
        DataWriteLimits limits;
        limits.MaxOutputBytes = TestAssetMaxBytes;
        JsonWriter writer(limits);
        std::string bytes;
        if (Serialize(writer, value, &error.Fields) == false || writer.Finish(bytes) == false)
        {
            error.Parse = writer.GetError();
            error.Stage = "serialize";
            return false;
        }
        static std::atomic<unsigned long long> sequence{0};
        bool created = false;
        for (unsigned attempt = 0; attempt < 128; ++attempt)
        {
            auto candidate = path;
            candidate += ".pending-" + std::to_string(sequence.fetch_add(1));
            std::error_code io;
            if (std::filesystem::create_directory(candidate, io))
            {
                temporary.Directory = std::move(candidate);
                temporary.File = temporary.Directory / "asset.json";
                created = true;
                break;
            }
            if (io && io != std::errc::file_exists) { break; }
        }
        if (created == false)
        {
            error.Stage = "create temporary";
            return false;
        }
        std::ofstream file(temporary.File, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        file.close();
        if (file.fail())
        {
            error.Stage = "write temporary";
            return false;
        }
        // 旧ファイルを削除・退避してからrenameする方式では、交換失敗時の復元も失敗し得ます。
        // 同一ボリューム内で完成済みファイルを置換し、失敗時は旧ファイルを保持します。
        // 電源断耐性・敵対的なPath変更・同時保存の競合解決はこの専用Assetの契約外です。
#ifdef _WIN32
        const bool replaced = MoveFileExW(temporary.File.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
        std::error_code io;
        std::filesystem::rename(temporary.File, path, io);
        const bool replaced = io.value() == 0;
#endif
        if (replaced == false)
        {
            error.Stage = "replace";
            return false;
        }
        return true;
    }
    catch (const std::exception&)
    {
        error.Stage = "save exception";
        return false;
    }
}
}
}
}
