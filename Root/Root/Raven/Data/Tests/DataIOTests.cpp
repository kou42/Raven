#include "Raven/Data/Tests/DataIOTests.h"
#include "Raven/Data/DataReader.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/Json/JsonWriter.h"
#include "Raven/Core/JsonParser.h"
#include "Raven/Core/JsonWriter.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#ifdef RAVEN_DATA_IO_ALLOCATION_TESTS
#include <cstdlib>
#include <new>

namespace
{
// 専用テスト実行ファイルだけで有効にする確保失敗注入。通常アプリのoperator newは変更しません。
std::int64_t allocationsUntilFailure = -1;
bool allocationFailed = false;
}
void* operator new(std::size_t size)
{
    if (allocationsUntilFailure == 0)
    {
        allocationFailed = true;
        throw std::bad_alloc();
    }
    if (allocationsUntilFailure > 0) { --allocationsUntilFailure; }
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory != nullptr) { return memory; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
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
void Check(bool condition, const char* description)
{
    ++checks;
    if (condition == false)
    {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

// JSON以外のSinkでも同じ型・イベント順を受け取れることと、失敗直後の停止を検査します。
class RecordingWriter final : public DataWriter
{
public:
    std::vector<std::string> Events;
    std::size_t FailAt = (std::numeric_limits<std::size_t>::max)();
    DataErrorCode FailureCode = DataErrorCode::OutputFailure;
    bool WriteNull() override { return Add("null"); }
    bool WriteBool(bool value) override { return Add(value ? "true" : "false"); }
    bool WriteInt64(std::int64_t value) override { return Add("int:" + std::to_string(value)); }
    bool WriteUInt64(std::uint64_t value) override { return Add("uint:" + std::to_string(value)); }
    bool WriteFloat64(double) override { return Add("float"); }
    bool WriteString(std::string_view value) override { return Add("string:" + std::string(value)); }
    bool WriteBinary(std::string_view value) override { return Add("binary:" + std::string(value)); }
    bool BeginArray() override { return Add("["); }
    bool EndArray() override { return Add("]"); }
    bool BeginObject() override { return Add("{"); }
    bool WriteKey(std::string_view key) override { return Add("key:" + std::string(key)); }
    bool EndObject() override { return Add("}"); }
    DataError GetError() const override { return m_Error; }
private:
    bool Add(std::string value)
    {
        Events.push_back(std::move(value));
        if (Events.size() - 1 == FailAt)
        {
            m_Error = {FailureCode, 999};
            return false;
        }
        return true;
    }
    DataError m_Error;
};

bool EmitSetting(DataWriter& writer)
{
    // 型付き設定の例。DocumentもJsonValueも構築せず、共通Writerへ直接書きます。
    return writer.BeginObject()
        && writer.WriteKey("version") && writer.WriteUInt64(1)
        && writer.WriteKey("label") && writer.WriteString("Raven\n設定")
        && writer.WriteKey("enabled") && writer.WriteBool(true)
        && writer.WriteKey("values") && writer.BeginArray()
        && writer.WriteInt64((std::numeric_limits<std::int64_t>::min)())
        && writer.WriteUInt64((std::numeric_limits<std::uint64_t>::max)())
        && writer.WriteFloat64(-0.0) && writer.WriteNull()
        && writer.EndArray() && writer.EndObject();
}

void RoundTrip(std::string_view json)
{
    DataDocument first;
    DataDocument second;
    DataError error;
    std::string encoded = "unchanged";
    std::string again;
    Check(JsonParser::Parse(json, first, &error), "round trip input parses");
    Check(JsonWriter::Write(first, encoded, &error) && error.Code == DataErrorCode::None,
        "Document serializes");
    Check(JsonParser::Parse(encoded, second, &error), "output parses with strict Data parser");
    RecordingWriter before;
    RecordingWriter after;
    Check(DataReader(first.GetRoot()).Read(before) && DataReader(second.GetRoot()).Read(after)
        && before.Events == after.Events, "round trip preserves typed event values and order");
    Check(JsonWriter::Write(second, again) && encoded == again, "output is canonical and idempotent");
}

void CheckFailure(JsonWriter& writer, bool result, DataErrorCode expected)
{
    std::string output = "preserved";
    Check(result == false && writer.GetError().Code == expected, "invalid event is rejected with expected code");
    Check(writer.Finish(output) == false && output == "preserved", "failed writer cannot publish partial output");
    Check(writer.WriteNull() == false && writer.GetError().Code == expected, "first failure remains sticky");
}
}

int RunDataIOTests()
{
    checks = 0;
    failures = 0;
    DataError error;
    DataDocument document;
    Check(JsonParser::Parse("{\"z\":[null,true,-1,18446744073709551615,0.25,\"a\\nb\"],\"\\u0061\":{}}",
        document, &error), "reader fixture parses");
    const std::vector<std::string> expected{
        "{", "key:z", "[", "null", "true", "int:-1", "uint:18446744073709551615",
        "float", "string:a\nb", "]", "key:a", "{", "}", "}"};
    RecordingWriter recorder;
    Check(DataReader(document.GetRoot()).Read(recorder, &error) && recorder.Events == expected,
        "reader emits decoded, typed, nested events in source order");
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        RecordingWriter failing;
        failing.FailAt = i;
        Check(DataReader(document.GetRoot()).Read(failing, &error) == false
            && error.Code == DataErrorCode::OutputFailure && error.Offset != 999
            && failing.Events.size() == i + 1, "sink failure stops at every event with source offset");
    }
    RecordingWriter unexplained;
    unexplained.FailAt = 0;
    unexplained.FailureCode = DataErrorCode::None;
    Check(DataReader(document.GetRoot()).Read(unexplained, &error) == false
        && error.Code == DataErrorCode::OutputFailure, "sink failure without error has explicit fallback");
    RecordingWriter invalid;
    Check(DataReader({}).Read(invalid, &error) == false && invalid.Events.empty()
        && error.Code == DataErrorCode::InvalidStructure, "invalid root produces no events");

    DataElement subtree;
    document.GetRoot().TryFind("z", subtree);
    std::string output;
    Check(JsonWriter::Write(subtree, output) && output == "[null,true,-1,18446744073709551615,0.25,\"a\\nb\"]\n",
        "subtree is serialized independently");
    Check(JsonWriter::Write(document, output)
        && output == "{\"a\":{},\"z\":[null,true,-1,18446744073709551615,0.25,\"a\\nb\"]}\n",
        "Document output sorts decoded keys");

    JsonWriter typed;
    Check(EmitSetting(typed) && typed.Finish(output), "typed data writes without DOM");
    Check(JsonParser::Parse(output, document), "typed settings parse");
    DataElement values;
    DataElement item;
    document.GetRoot().TryFind("values", values);
    values.TryGetArrayElement(0, item);
    std::int64_t signedValue = 0;
    Check(item.TryGetInt64(signedValue) && signedValue == (std::numeric_limits<std::int64_t>::min)(),
        "Int64 minimum preserves precision");
    values.TryGetArrayElement(1, item);
    std::uint64_t unsignedValue = 0;
    Check(item.TryGetUInt64(unsignedValue) && unsignedValue == (std::numeric_limits<std::uint64_t>::max)(),
        "UInt64 maximum preserves precision");
    values.TryGetArrayElement(2, item);
    double floating = 1;
    Check(item.IsType(DataType::Float64) && item.TryGetFloat64(floating) && std::signbit(floating),
        "negative float zero preserves type and sign");

    for (const auto* fixture : {"null", "true", "false", "-9223372036854775808", "18446744073709551615",
        "9007199254740993", "[]", "{}", "[[],{},[null]]", "\"\\uD83D\\uDE00\\u0000\\n\\\\\\\"\"",
        "{\"a\":1,\"b\":[{\"x\":\"設定\"}]}"})
    {
        RoundTrip(fixture);
    }
    for (double value : {0.0, -0.0, 1.0, -1.0, 0.1, 0x1p53, 0x1p63,
        (std::numeric_limits<double>::denorm_min)(), (std::numeric_limits<double>::min)(),
        (std::numeric_limits<double>::max)(), -(std::numeric_limits<double>::max)()})
    {
        JsonWriter writer;
        Check(writer.WriteFloat64(value) && writer.Finish(output) && JsonParser::Parse(output, document),
            "finite float including subnormal parses after output");
        floating = 0;
        Check(document.GetRoot().IsType(DataType::Float64) && document.GetRoot().TryGetFloat64(floating)
            && std::memcmp(&floating, &value, sizeof(value)) == 0, "float bits survive round trip");
    }
    for (double value : {(std::numeric_limits<double>::quiet_NaN)(),
        (std::numeric_limits<double>::infinity)(), -(std::numeric_limits<double>::infinity)()})
    {
        JsonWriter writer;
        CheckFailure(writer, writer.WriteFloat64(value), DataErrorCode::NumericRange);
    }

    // すべての制御Byte、NUL、Unicodeを同じDecoded文字列として保存します。
    std::string controls;
    for (int i = 0; i < 32; ++i) { controls += static_cast<char>(i); }
    controls += "\"\\設定😀";
    JsonWriter stringWriter;
    Check(stringWriter.WriteString(controls) && stringWriter.Finish(output) && JsonParser::Parse(output, document),
        "all JSON string escapes are valid");
    std::string decoded;
    Check(document.GetRoot().TryGetDecodedString(decoded) && decoded == controls, "decoded string bytes round trip");
    for (const auto& bytes : {std::string("\x80"), std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
        std::string("\xf4\x90\x80\x80"), std::string("\xe3\x81")})
    {
        JsonWriter writer;
        CheckFailure(writer, writer.WriteString(bytes), DataErrorCode::InvalidEncoding);
        JsonWriter keyWriter;
        keyWriter.BeginObject();
        CheckFailure(keyWriter, keyWriter.WriteKey(bytes), DataErrorCode::InvalidEncoding);
    }

    { JsonWriter w; CheckFailure(w, w.EndArray(), DataErrorCode::InvalidStructure); }
    { JsonWriter w; CheckFailure(w, w.EndObject(), DataErrorCode::InvalidStructure); }
    { JsonWriter w; CheckFailure(w, w.WriteKey("x"), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.BeginArray(); CheckFailure(w, w.WriteKey("x"), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.BeginArray(); CheckFailure(w, w.EndObject(), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.BeginObject(); CheckFailure(w, w.WriteNull(), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.BeginObject(); w.WriteKey("x"); CheckFailure(w, w.EndObject(), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.BeginObject(); w.WriteKey("x"); CheckFailure(w, w.WriteKey("y"), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.WriteNull(); CheckFailure(w, w.WriteNull(), DataErrorCode::InvalidStructure); }
    { JsonWriter w; CheckFailure(w, w.Finish(output), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.BeginArray(); CheckFailure(w, w.Finish(output), DataErrorCode::InvalidStructure); }
    { JsonWriter w; w.WriteNull(); w.Finish(output); CheckFailure(w, w.WriteNull(), DataErrorCode::InvalidStructure); }
    {
        JsonWriter w;
        w.BeginObject(); w.WriteKey("x"); w.WriteNull(); w.WriteKey("x"); w.BeginArray(); w.EndArray();
        CheckFailure(w, w.EndObject(), DataErrorCode::DuplicateKey);
    }
    { JsonWriter w; CheckFailure(w, w.WriteBinary("bytes"), DataErrorCode::UnsupportedType); }
    DataDocument binary;
    Check(DataDocument::Create(std::string("\0\xff", 2), {{DataType::Binary, 0, 2, 1, 0, 0}}, binary),
        "binary fixture creates");
    RecordingWriter binaryRecorder;
    Check(DataReader(binary.GetRoot()).Read(binaryRecorder)
        && binaryRecorder.Events == std::vector<std::string>{std::string("binary:\0\xff", 9)},
        "format independent reader preserves Binary bytes");
    output = "preserved";
    Check(JsonWriter::Write(binary, output, &error) == false && output == "preserved"
        && error.Code == DataErrorCode::UnsupportedType, "JSON explicitly rejects Binary and preserves output");

    // 出力予算は改行、引用符、Escape、キー、括弧、区切りをすべて含みます。
    for (const auto* fixture : {"null", "\"\"", "\"\\u0000\"", "[1,2,[3]]", "{\"b\":{},\"a\":[1,2]}"})
    {
        JsonParser::Parse(fixture, document);
        std::string full;
        JsonWriter::Write(document, full);
        for (std::size_t size = 0; size <= full.size() + 1; ++size)
        {
            DataWriteLimits limits;
            limits.MaxOutputBytes = static_cast<std::uint32_t>(size);
            output = "preserved";
            const bool result = JsonWriter::Write(document, output, &error, limits);
            Check(size >= full.size() ? result && output == full
                : result == false && output == "preserved" && error.Code == DataErrorCode::LimitExceeded,
                "exact output budget boundary");
        }
    }
    {
        DataWriteLimits limits;
        limits.MaxDepth = 0;
        JsonWriter scalar(limits);
        Check(scalar.WriteNull() && scalar.Finish(output), "zero container depth allows scalar");
        JsonWriter container(limits);
        CheckFailure(container, container.BeginArray(), DataErrorCode::LimitExceeded);
        limits.MaxDepth = 1;
        JsonWriter nested(limits);
        nested.BeginObject(); nested.WriteKey("x");
        CheckFailure(nested, nested.BeginArray(), DataErrorCode::LimitExceeded);
        limits.MaxContainerElements = 0;
        JsonWriter empty(limits);
        Check(empty.BeginArray() && empty.EndArray() && empty.Finish(output), "zero elements permits empty container");
        JsonWriter array(limits);
        array.BeginArray();
        CheckFailure(array, array.WriteNull(), DataErrorCode::LimitExceeded);
        JsonWriter object(limits);
        object.BeginObject();
        CheckFailure(object, object.WriteKey("x"), DataErrorCode::LimitExceeded);
        limits.MaxContainerElements = 1;
        JsonWriter one(limits);
        one.BeginArray(); one.WriteNull();
        CheckFailure(one, one.WriteNull(), DataErrorCode::LimitExceeded);
        limits.MaxValueBytes = 2;
        JsonWriter bytes(limits);
        Check(bytes.WriteString("ab") && bytes.Finish(output), "exact decoded string byte limit accepted");
        JsonWriter tooLong(limits);
        CheckFailure(tooLong, tooLong.WriteString("設定"), DataErrorCode::LimitExceeded);
    }

    // Core Writerとの共有規約（キー順、String Escape、改行）を独立経路で照合します。
    const std::string coreFixture = "{\"z\":\"a\\nb\\u0000\",\"a\":[null,false,1.25,{\"y\":\"設定\",\"x\":true}]}";
    Core::JsonValue core;
    std::string coreText;
    Check(Core::JsonParser::Parse(coreFixture, core) && Core::JsonWriter::Write(core, coreText, nullptr, 0)
        && JsonParser::Parse(coreFixture, document) && JsonWriter::Write(document, output)
        && output == coreText, "existing Core Writer compatible deterministic compact output");
    Check(DataDocument::FromJsonValue(core, document) && JsonWriter::Write(document, output)
        && output == coreText, "existing JsonValue adapter connects to the same Writer");

    // 走査は再帰やindex検索を使わず、大配列・深い構造でも全要素を一度ずつ処理します。
    std::string large = "[";
    for (int i = 0; i < 10000; ++i) { large += i == 0 ? "0" : ",0"; }
    large += ']';
    Check(JsonParser::Parse(large, document) && JsonWriter::Write(document, output) && output == large + "\n",
        "large array traverses and writes");
    DataLimits parseLimits;
    parseLimits.MaxDepth = 512;
    DataWriteLimits writeLimits;
    writeLimits.MaxDepth = 512;
    const std::string deep = std::string(512, '[') + "null" + std::string(512, ']');
    Check(JsonParser::Parse(deep, document, &error, parseLimits)
        && JsonWriter::Write(document, output, &error, writeLimits) && output == deep + "\n",
        "deep document traverses iteratively");

    // 固定Seedのbit列で、境界Fixture以外の有限double / UInt64も往復検査します。
    std::uint64_t bits = 0x123456789abcdef0ull;
    for (int i = 0; i < 2000; ++i)
    {
        bits ^= bits << 13u;
        bits ^= bits >> 7u;
        bits ^= bits << 17u;
        double original = 0;
        std::memcpy(&original, &bits, sizeof(original));
        if (std::isfinite(original) == true)
        {
            JsonWriter writer;
            double restored = 0;
            Check(writer.WriteFloat64(original) && writer.Finish(output) && JsonParser::Parse(output, document)
                && document.GetRoot().TryGetFloat64(restored)
                && std::memcmp(&original, &restored, sizeof(original)) == 0,
                "generated finite double preserves bits");
        }
        JsonWriter writer;
        std::uint64_t restored = 0;
        Check(writer.WriteUInt64(bits) && writer.Finish(output) && JsonParser::Parse(output, document)
            && document.GetRoot().TryGetUInt64(restored) && restored == bits,
            "generated UInt64 preserves integer precision");
    }

#ifdef RAVEN_DATA_IO_ALLOCATION_TESTS
    const std::string allocationFixture =
        "{\"long escaped key \\u0061 with allocation\":[\"long escaped value \\u0062 with allocation\","
        "{\"another long key with allocation\":\"another long value with allocation\"}]}";
    Check(JsonParser::Parse(allocationFixture, document), "allocation fixture parses before injection");
    int injectedFailures = 0;
    for (int route = 0; route < 2; ++route)
    {
        bool exhausted = false;
        for (int index = 0; index < 256; ++index)
        {
            std::string preserved = "original output remains unchanged";
            JsonWriter writer;
            allocationFailed = false;
            allocationsUntilFailure = index;
            const bool success = route == 0
                ? JsonWriter::Write(document, preserved, &error)
                : EmitSetting(writer) && writer.Finish(preserved);
            allocationsUntilFailure = -1;
            if (allocationFailed == false)
            {
                Check(success, "operation succeeds after all allocation failure points");
                exhausted = true;
                break;
            }
            ++injectedFailures;
            const auto code = route == 0 ? error.Code : writer.GetError().Code;
            Check(success == false && preserved == "original output remains unchanged"
                && code == DataErrorCode::OutOfMemory, "allocation failure preserves caller output and reports OOM");
        }
        Check(exhausted, "all allocation sites enumerated within bound");
    }
    std::cout << "Allocation failure sites: " << injectedFailures << '\n';
#endif

    std::cout << "Data IO: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}

#ifdef RAVEN_DATA_IO_TEST_MAIN
int main()
{
    return Raven::Data::Tests::RunDataIOTests();
}
#endif
