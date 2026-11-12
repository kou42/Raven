#include "Raven/Data/Tests/SerializationTests.h"
#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include "Raven/Data/Serialization/Serialization.h"
#include "Raven/Data/DataDocument.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/Json/JsonWriter.h"
#include <iostream>

#ifdef RAVEN_SERIALIZATION_ALLOCATION_TESTS
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
// フォーマット非依存の失敗Sink。型Adapterがfalseの直後に停止することを検証します。
class FailingWriter final : public DataWriter
{
public:
    std::size_t Calls = 0;
    std::size_t FailAt = 0;
    bool WriteNull() override { return Event(); }
    bool WriteBool(bool) override { return Event(); }
    bool WriteInt64(std::int64_t) override { return Event(); }
    bool WriteUInt64(std::uint64_t) override { return Event(); }
    bool WriteFloat64(double) override { return Event(); }
    bool WriteString(std::string_view) override { return Event(); }
    bool WriteBinary(std::string_view) override { return Event(); }
    bool BeginArray() override { return Event(); }
    bool EndArray() override { return Event(); }
    bool BeginObject() override { return Event(); }
    bool WriteKey(std::string_view) override { return Event(); }
    bool EndObject() override { return Event(); }
    DataError GetError() const override { return {}; }
private:
    bool Event() { return Calls++ != FailAt; }
};
int checks = 0;
int failures = 0;
void Check(bool condition, const char* description)
{
    ++checks;
    if (condition == false) { ++failures; std::cerr << "FAIL: " << description << '\n'; }
}
bool Load(std::string_view text, TestSettings& output, SerializationError& error)
{
    DataDocument document;
    if (JsonParser::Parse(text, document) == false) { return false; }
    return Deserialize(document.GetRoot(), output, &error);
}
template<class T> void ReadCase(std::string_view text, T initial, bool success, T expected)
{
    DataDocument document;
    Check(JsonParser::Parse(text, document), "scalar fixture parses");
    SerializationError error;
    Check(Deserialize(document.GetRoot(), initial, &error) == success, "scalar result");
    Check(initial == expected, "scalar value / preserved output");
}
}
int RunSerializationTests()
{
    checks = 0; failures = 0;
    SerializationError error;
    TestSettings settings{false, "preserved", {99}};
    const std::string prefix = "{\"type\":\"Raven.StructuredDataTest\",\"version\":2,";
    Check(Load(prefix + "\"enabled\":true}", settings, error), "optional defaults");
    Check(settings.Enabled && settings.Label.empty() && settings.Values.empty(), "missing fields reset defaults");
    Check(Load("{\"type\":\"Raven.StructuredDataTest\",\"version\":1,\"enabled\":true,\"samples\":[1,-2,3]}", settings, error), "v1 migration");
    Check(settings.Values == std::vector<int>({1,-2,3}), "v1 values preserved");
    JsonWriter writer;
    std::string text = "preserved";
    Check(Serialize(writer, settings, &error) && writer.Finish(text), "typed writer completes");
    Check(text.find("\"version\":2") != std::string::npos, "writes latest version");
    TestSettings restored;
    Check(Load(text, restored, error) && restored.Enabled == settings.Enabled
        && restored.Label == settings.Label && restored.Values == settings.Values, "typed round trip");
    struct Invalid { const char* Body; const char* Path; SerializationErrorCode Code; };
    for (const auto& bad : {
        Invalid{"\"label\":\"x\"}", "/enabled", SerializationErrorCode::MissingField},
        Invalid{"\"enabled\":1}", "/enabled", SerializationErrorCode::TypeMismatch},
        Invalid{"\"enabled\":true,\"label\":null}", "/label", SerializationErrorCode::TypeMismatch},
        Invalid{"\"enabled\":true,\"values\":[1,\"bad\"]}", "/values/1", SerializationErrorCode::TypeMismatch},
        Invalid{"\"enabled\":true,\"values\":[2147483648]}", "/values/0", SerializationErrorCode::NumericRange},
        Invalid{"\"enabled\":true,\"values\":null}", "/values", SerializationErrorCode::TypeMismatch},
        Invalid{"\"enabled\":true,\"label\":\"012345678901234567890123456789012\"}", "/label", SerializationErrorCode::ValidationFailed}})
    {
        settings = {false, "preserved", {99}};
        Check(Load(prefix + bad.Body, settings, error) == false, "invalid setting rejected");
        Check(error.Code == bad.Code && std::string(error.Path.data()) == bad.Path, "field error code and path");
        Check(settings.Enabled == false && settings.Label == "preserved" && settings.Values == std::vector<int>{99}, "failure preserves complete setting");
    }
    for (const auto version : {"0", "3", "-1", "1.0", "null", "\"2\""})
    {
        Check(Load(std::string("{\"type\":\"Raven.StructuredDataTest\",\"version\":") + version + ",\"enabled\":true}", settings, error) == false,
            "invalid / unsupported version rejected");
        Check(std::string(error.Path.data()) == "/version", "version path");
    }
    Check(Load("{\"type\":\"wrong\",\"version\":2,\"enabled\":true}", settings, error) == false
        && std::string(error.Path.data()) == "/type", "type discriminator checked");
    Check(Load("{\"version\":2,\"enabled\":true}", settings, error) == false
        && error.Code == SerializationErrorCode::MissingField, "required discriminator");
    Check(Load(prefix + "\"enabled\":true,\"unknown\":{\"nested\":[null]}}", settings, error), "unknown fields ignored");
    Check(Load(prefix + "\"enabl\\u0065d\":true,\"label\":\"a\\u0000b\"}", settings, error)
        && settings.Label == std::string("a\0b",3), "escaped key / owned NUL string");
    Check(Load("[]", settings, error) == false && error.Code == SerializationErrorCode::TypeMismatch, "object required");
    ReadCase<int>("2147483647", 9, true, 2147483647);
    ReadCase<int>("-2147483648", 9, true, (-2147483647-1));
    ReadCase<int>("2147483648", 9, false, 9);
    ReadCase<unsigned>("-1", 9, false, 9);
    ReadCase<int>("1.5", 9, false, 9);
    ReadCase<int>("2.0", 9, true, 2);
    ReadCase<bool>("1", false, false, false);
    ReadCase<double>("9007199254740993", 9, false, 9);
    ReadCase<float>("0.1", 9, false, 9);
    ReadCase<float>("0.5", 9, true, 0.5f);
    ReadCase<float>("1e100", 9, false, 9);
    ReadCase<std::uint64_t>("18446744073709551615", 9, true, UINT64_MAX);
    ReadCase<std::int64_t>("18446744073709551615", 9, false, 9);
    DataDocument document;
    Check(JsonParser::Parse("[[1],[2, false]]", document), "nested array parses");
    std::vector<std::vector<int>> nested{{7}};
    Check(Deserialize(document.GetRoot(), nested, &error) == false && nested == std::vector<std::vector<int>>{{7}}
        && std::string(error.Path.data()) == "/1/1", "nested arrays preserve output and path");
    Check(Deserialize(DataElement{}, settings, &error) == false, "invalid element rejected");
    SerializationError pathError;
    SerializationContext context(pathError);
    auto child = context.Field("a/b~c").Index(12);
    child.Fail(SerializationErrorCode::ValidationFailed);
    Check(std::string(pathError.Path.data()) == "/a~1b~0c/12", "JSON pointer escape");
    context.Fail(SerializationErrorCode::TypeMismatch);
    Check(pathError.Code == SerializationErrorCode::ValidationFailed, "first error sticky");
    pathError = {};
    auto tooLong = context.Field(std::string(512, 'x'));
    Check(tooLong.Good() == false && pathError.Code == SerializationErrorCode::LimitExceeded, "path overflow rejected");
    pathError = {};
    auto boundary = context.Field(std::string(510, 'x'));
    Check(boundary.Good(), "511 byte path accepted");
    boundary.Index(0);
    Check(context.Good() == false, "child beyond path boundary rejected");
    pathError = {};
    context.Field(std::string_view("a\0b", 3));
    Check(context.Good() == false, "NUL path rejected explicitly");
    Check(JsonParser::Parse("[true,false]", document), "bool vector fixture");
    std::vector<bool> bits;
    Check(Deserialize(document.GetRoot(), bits, &error) && bits == std::vector<bool>({true,false}), "bool vector read");
    JsonWriter boolWriter;
    Check(Serialize(boolWriter, bits, &error) && boolWriter.Finish(text) && text == "[true,false]\n", "bool vector write");
    JsonWriter nonFinite;
    Check(Serialize(nonFinite, (std::numeric_limits<double>::infinity)(), &error) == false
        && error.Code == SerializationErrorCode::NumericRange, "non finite rejected before event");
    Check(JsonParser::Parse(prefix + "\"enabled\":true,\"values\":[1,2]}", document), "error position fixture");
    TestSettings positionOutput;
    Check(Deserialize(document.GetRoot(), positionOutput, &error), "error cleared after success");
    Check(error.Code == SerializationErrorCode::None && error.Path[0] == '\0', "success resets diagnostics");
    settings = {true, "test", {1,2}};
    FailingWriter successfulSink;
    successfulSink.FailAt = (std::numeric_limits<std::size_t>::max)();
    Check(Serialize(successfulSink, settings, &error), "generic writer success");
    for (std::size_t failAt = 0; failAt < successfulSink.Calls; ++failAt)
    {
        FailingWriter sink;
        sink.FailAt = failAt;
        Check(Serialize(sink, settings, &error) == false && sink.Calls == failAt + 1
            && error.Code == SerializationErrorCode::OutputFailure, "stop immediately on each sink failure");
    }
    DataWriteLimits limits; limits.MaxOutputBytes = 4;
    JsonWriter limited(limits);
    text = "preserved";
    Check(Serialize(limited, settings, &error) == false && limited.Finish(text) == false
        && text == "preserved" && error.Code == SerializationErrorCode::OutputFailure, "writer limit and output preservation");
    JsonWriter invalidWriter;
    settings.Label = std::string(33,'x');
    Check(Serialize(invalidWriter, settings, &error) == false && std::string(error.Path.data()) == "/label", "serialize semantic validation");
#ifdef RAVEN_SERIALIZATION_ALLOCATION_TESTS
    Check(JsonParser::Parse(prefix + "\"enabled\":true,\"label\":\"012345678901234567890123456789\",\"values\":[1,2,3,4]}", document), "allocation fixture");
    bool completed = false;
    for (int failAt = 0; failAt < 100; ++failAt)
    {
        settings = {false, "preserved", {99}};
        allocationFailed = false;
        allocationsUntilFailure = failAt;
        const bool success = Deserialize(document.GetRoot(), settings, &error);
        allocationsUntilFailure = -1;
        if (allocationFailed == false) { Check(success, "allocation sweep completes"); completed = true; break; }
        Check(success == false && error.Code == SerializationErrorCode::OutOfMemory
            && settings.Label == "preserved" && settings.Values == std::vector<int>{99}, "allocation failure preserves output");
    }
    Check(completed, "all deserialize allocation sites injected");
    settings = {true, "long enough label for allocation", {1,2,3}};
    completed = false;
    for (int failAt = 0; failAt < 200; ++failAt)
    {
        JsonWriter allocationWriter;
        text = "preserved";
        allocationFailed = false;
        allocationsUntilFailure = failAt;
        const bool success = Serialize(allocationWriter, settings, &error);
        const bool finished = success && allocationWriter.Finish(text);
        allocationsUntilFailure = -1;
        if (allocationFailed == false) { Check(finished, "writer allocation sweep completes"); completed = true; break; }
        Check(finished == false && text == "preserved", "writer allocation failure preserves output");
    }
    Check(completed, "all serialize allocation sites injected");

#endif
    std::cout << "Serialization: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}
#ifdef RAVEN_SERIALIZATION_TEST_MAIN
int main() { return Raven::Data::Tests::RunSerializationTests(); }
#endif
