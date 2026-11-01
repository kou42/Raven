#include "Raven/Data/Tests/MessagePackTests.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/Json/JsonWriter.h"
#include "Raven/Data/Serialization/Serialization.h"
#include <cmath>
#include <iostream>
#include <limits>

#ifdef RAVEN_MESSAGEPACK_ALLOCATION_TESTS
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
void Check(bool condition, const char* label)
{
    ++checks;
    if (condition == false) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
std::string Bytes(std::initializer_list<unsigned> bytes)
{
    std::string result;
    for (unsigned byte : bytes) { result += static_cast<char>(byte); }
    return result;
}
}
int RunMessagePackTests()
{
    checks = 0; failures = 0;
    DataDocument document;
    DataError error;
    std::string output;
    // MessagePack specificationの全整数幅・境界。Writerの最短表現も固定Fixtureと比較します。
    const std::vector<std::pair<std::int64_t, std::string>> signedCases = {
        {-1, Bytes({0xff})}, {-32, Bytes({0xe0})}, {-33, Bytes({0xd0,0xdf})},
        {-128, Bytes({0xd0,0x80})}, {-129, Bytes({0xd1,0xff,0x7f})},
        {-32768, Bytes({0xd1,0x80,0})}, {-32769, Bytes({0xd2,0xff,0xff,0x7f,0xff})},
        {INT32_MIN, Bytes({0xd2,0x80,0,0,0})},
        {INT64_C(-2147483649), Bytes({0xd3,0xff,0xff,0xff,0xff,0x7f,0xff,0xff,0xff})},
        {INT64_MIN, Bytes({0xd3,0x80,0,0,0,0,0,0,0})}};
    for (const auto& item : signedCases)
    {
        MessagePackWriter writer;
        Check(writer.WriteInt64(item.first) && writer.Finish(output) && output == item.second, "signed encoding");
        std::int64_t value = 0;
        Check(MessagePackParser::Parse(item.second, document) && document.GetRoot().TryGetInt64(value)
            && value == item.first, "signed parsing");
    }
    const std::vector<std::pair<std::uint64_t, std::string>> unsignedCases = {
        {0, Bytes({0})}, {127, Bytes({127})}, {128, Bytes({0xcc,128})}, {255, Bytes({0xcc,255})},
        {256, Bytes({0xcd,1,0})}, {65535, Bytes({0xcd,255,255})}, {65536, Bytes({0xce,0,1,0,0})},
        {UINT32_MAX, Bytes({0xce,255,255,255,255})},
        {UINT64_C(4294967296), Bytes({0xcf,0,0,0,1,0,0,0,0})},
        {UINT64_MAX, Bytes({0xcf,255,255,255,255,255,255,255,255})}};
    for (const auto& item : unsignedCases)
    {
        MessagePackWriter writer;
        Check(writer.WriteUInt64(item.first) && writer.Finish(output) && output == item.second, "unsigned encoding");
        std::uint64_t value = 0;
        Check(MessagePackParser::Parse(item.second, document) && document.GetRoot().TryGetUInt64(value)
            && value == item.first, "unsigned parsing");
    }
    // 非最短表現の正のsigned整数も許容します。
    for (const auto& bytes : {Bytes({0xd0,1}), Bytes({0xd1,0,1}), Bytes({0xd2,0,0,0,1}), Bytes({0xd3,0,0,0,0,0,0,0,1})})
    {
        std::int64_t value = 0;
        Check(MessagePackParser::Parse(bytes, document) && document.GetRoot().IsType(DataType::Int64)
            && document.GetRoot().TryGetInt64(value) && value == 1, "positive signed representation");
    }
    for (const auto& bytes : {Bytes({0xca,0x3f,0xc0,0,0}), Bytes({0xcb,0x3f,0xf8,0,0,0,0,0,0})})
    {
        double value = 0;
        Check(MessagePackParser::Parse(bytes, document) && document.GetRoot().TryGetFloat64(value) && value == 1.5, "float widths");
    }
    for (double number : {0.0, -0.0, 1.5, (std::numeric_limits<double>::max)(), std::numeric_limits<double>::denorm_min()})
    {
        MessagePackWriter writer;
        double value = 1;
        Check(writer.WriteFloat64(number) && writer.Finish(output) && output.size() == 9
            && MessagePackParser::Parse(output, document) && document.GetRoot().TryGetFloat64(value)
            && value == number && std::signbit(value) == std::signbit(number), "float64 round trip");
    }
    for (unsigned size : {0u,1u,31u,32u,255u,256u,65535u,65536u})
    {
        for (bool binary : {false,true})
        {
            const std::string value(size, binary ? static_cast<char>(0xff) : 'x');
            MessagePackWriter writer;
            Check((binary ? writer.WriteBinary(value) : writer.WriteString(value)) && writer.Finish(output), "string/binary encode");
            const unsigned tag = binary ? (size <= 255u ? 0xc4u : (size <= 65535u ? 0xc5u : 0xc6u))
                : (size < 32u ? 0xa0u | size : (size <= 255u ? 0xd9u : (size <= 65535u ? 0xdau : 0xdbu)));
            Check(static_cast<unsigned char>(output[0]) == tag, "string/binary prefix");
            std::string_view decoded;
            Check(MessagePackParser::Parse(output, document) && document.GetRoot().IsType(binary ? DataType::Binary : DataType::String)
                && document.GetRoot().TryGetSource(decoded) && decoded == value, "string/binary parse");
            DataWriteLimits limit; limit.MaxOutputBytes = static_cast<std::uint32_t>(output.size());
            MessagePackWriter exact(limit);
            Check((binary ? exact.WriteBinary(value) : exact.WriteString(value)) && exact.Finish(output), "exact output size");
            --limit.MaxOutputBytes; MessagePackWriter shortWriter(limit);
            Check((binary ? shortWriter.WriteBinary(value) : shortWriter.WriteString(value)) == false, "one short output size");
        }
    }
    for (unsigned count : {0u,15u,16u,65535u,65536u})
    {
        for (bool object : {false,true})
        {
            MessagePackWriter writer;
            Check(object ? writer.BeginObject() : writer.BeginArray(), "begin container");
            bool good = true;
            for (unsigned i = 0; i < count; ++i)
            {
                if (object == true && writer.WriteKey(std::to_string(i)) == false) { good = false; break; }
                if (writer.WriteNull() == false) { good = false; break; }
            }
            Check(good && (object ? writer.EndObject() : writer.EndArray()) && writer.Finish(output), "container encode");
            const unsigned tag = count < 16u ? (object ? 0x80u : 0x90u) | count
                : (object ? (count <= 65535u ? 0xdeu : 0xdfu) : (count <= 65535u ? 0xdcu : 0xddu));
            std::uint32_t actual = 0;
            Check(static_cast<unsigned char>(output[0]) == tag && MessagePackParser::Parse(output, document)
                && document.GetRoot().TryGetCount(actual) && actual == count, "container count prefix");
        }
    }
    const std::string nested = Bytes({0x82,0xa1,'b',0x92,0xc0,0xc3,0xa1,'a',0xc2});
    Check(MessagePackParser::Parse(nested, document) && MessagePackWriter::Write(document, output)
        && output == Bytes({0x82,0xa1,'a',0xc2,0xa1,'b',0x92,0xc0,0xc3}), "nested deterministic object");
    // 各fixtureの全切断位置を拒否し、元Documentを維持します。
    std::vector<std::string> fixtures = {nested, Bytes({0xd9,1,'x'}), Bytes({0xda,0,1,'x'}), Bytes({0xdb,0,0,0,1,'x'}),
        Bytes({0xc4,1,255}), Bytes({0xc5,0,1,255}), Bytes({0xc6,0,0,0,1,255}),
        Bytes({0xdc,0,1,0xc0}), Bytes({0xdd,0,0,0,1,0xc0}),
        Bytes({0xde,0,1,0xa0,0xc0}), Bytes({0xdf,0,0,0,1,0xa0,0xc0}),
        Bytes({0xca,0,0,0,1}), Bytes({0xcb,0,0,0,0,0,0,0,1})};
    for (const auto& item : signedCases) { fixtures.push_back(item.second); }
    for (const auto& item : unsignedCases) { fixtures.push_back(item.second); }
    for (const auto& bytes : fixtures)
    {
        Check(MessagePackParser::Parse(bytes, document), "valid fixture");
        for (std::size_t end = 0; end < bytes.size(); ++end)
        {
            const auto source = document.GetSource();
            Check(MessagePackParser::Parse(std::string_view(bytes).substr(0,end), document, &error) == false
                && error.Code == DataErrorCode::UnexpectedEnd && error.Offset == end
                && document.GetSource() == source, "truncation preserves document");
        }
    }
    const std::vector<std::pair<std::string, DataErrorCode>> invalid = {
        {Bytes({0xc1}),DataErrorCode::InvalidSyntax}, {Bytes({0xc0,0xc0}),DataErrorCode::TrailingData},
        {Bytes({0x81,0,0}),DataErrorCode::UnsupportedType}, {Bytes({0x81,0xc4,0,0}),DataErrorCode::UnsupportedType},
        {Bytes({0x82,0xa1,'a',0,0xd9,1,'a',1}),DataErrorCode::DuplicateKey},
        {Bytes({0xa1,0xff}),DataErrorCode::InvalidEncoding}, {Bytes({0xa2,0xc0,0x80}),DataErrorCode::InvalidEncoding},
        {Bytes({0xa3,0xed,0xa0,0x80}),DataErrorCode::InvalidEncoding},
        {Bytes({0xa4,0xf4,0x90,0x80,0x80}),DataErrorCode::InvalidEncoding},
        {Bytes({0xca,0x7f,0x80,0,0}),DataErrorCode::NumericRange}, {Bytes({0xca,0x7f,0xc0,0,0}),DataErrorCode::NumericRange},
        {Bytes({0xcb,0xff,0xf0,0,0,0,0,0,0}),DataErrorCode::NumericRange},
        {Bytes({0xcb,0x7f,0xf8,0,0,0,0,0,0}),DataErrorCode::NumericRange},
        {Bytes({0xdb,255,255,255,255}),DataErrorCode::LimitExceeded},
        {Bytes({0xc6,255,255,255,255}),DataErrorCode::LimitExceeded},
        {Bytes({0xdd,255,255,255,255}),DataErrorCode::LimitExceeded},
        {Bytes({0xdf,255,255,255,255}),DataErrorCode::LimitExceeded}};
    for (const auto& item : invalid)
    {
        const std::string before(document.GetSource());
        Check(MessagePackParser::Parse(item.first, document, &error) == false && error.Code == item.second
            && document.GetSource() == before, "invalid input classification / no commit");
    }
    for (unsigned tag : {0xc7u,0xc8u,0xc9u,0xd4u,0xd5u,0xd6u,0xd7u,0xd8u})
    {
        Check(MessagePackParser::Parse(Bytes({tag,0,0,0,0,0,0,0,0,0}), document, &error) == false
            && error.Code == DataErrorCode::UnsupportedType, "extensions rejected including timestamp");
    }
    // JSON経由で表現可能な範囲だけ相互変換し、Binaryは暗黙文字列化しません。
    Check(JsonParser::Parse("{\"k\\u0000\": [18446744073709551615,-9223372036854775808,1.5,\"日本語\\n\"]}", document)
        && MessagePackWriter::Write(document, output) && MessagePackParser::Parse(output, document)
        && JsonWriter::Write(document, output), "JSON / MessagePack shared domain");
    Check(MessagePackParser::Parse(Bytes({0xc4,1,255}), document), "binary document");
    output = "preserved";
    Check(JsonWriter::Write(document, output, &error) == false && error.Code == DataErrorCode::UnsupportedType
        && output == "preserved", "JSON binary loss detected");
    // 既存Adapterをそのまま使用する型付きメッセージ。
    const std::vector<std::vector<std::int64_t>> message = {{INT64_MIN,0,INT64_MAX},{1,-2,3}};
    MessagePackWriter typedWriter;
    std::vector<std::vector<std::int64_t>> restored;
    Check(Serialize(typedWriter, message) && typedWriter.Finish(output) && MessagePackParser::Parse(output, document)
        && Deserialize(document.GetRoot(), restored) && restored == message, "typed round trip");
    restored = {{99}};
    Check(MessagePackParser::Parse(Bytes({0x91,0xa0}), document) && Deserialize(document.GetRoot(), restored) == false
        && restored == std::vector<std::vector<std::int64_t>>{{99}}, "typed failure preserves output");
    // 所有Source自身を入力に渡してもCommitまで参照を維持します。
    Check(MessagePackParser::Parse(document.GetSource(), document), "source alias parse");
    const std::string deep = std::string(128, static_cast<char>(0x91)) + Bytes({0xc0});
    Check(MessagePackParser::Parse(deep, document), "depth exact boundary");
    Check(MessagePackParser::Parse(Bytes({0x91}) + deep, document, &error) == false
        && error.Code == DataErrorCode::LimitExceeded, "depth exceeded");
    DataLimits limits; limits.MaxTokens = 2;
    Check(MessagePackParser::Parse(Bytes({0x91,0}), document, &error, limits), "tokens exact boundary");
    limits.MaxTokens = 1;
    Check(MessagePackParser::Parse(Bytes({0x91,0}), document, &error, limits) == false && error.Code == DataErrorCode::LimitExceeded, "token limit");
    limits = {}; limits.MaxContainerElements = 1;
    Check(MessagePackParser::Parse(Bytes({0x91,0}), document, &error, limits), "count exact");
    Check(MessagePackParser::Parse(Bytes({0x92,0,0}), document, &error, limits) == false, "count excess");
    limits = {}; limits.MaxValueBytes = 1;
    Check(MessagePackParser::Parse(Bytes({0xa1,'a'}), document, &error, limits), "value exact");
    Check(MessagePackParser::Parse(Bytes({0xa2,'a','a'}), document, &error, limits) == false, "value excess");
    limits = {}; limits.MaxInputBytes = 1;
    Check(MessagePackParser::Parse(Bytes({0xc0}), document, &error, limits), "input exact");
    Check(MessagePackParser::Parse(Bytes({0xa1,'a'}), document, &error, limits) == false, "input excess");
    limits = {}; limits.MaxTokens = InvalidTokenId;
    Check(MessagePackParser::Parse(Bytes({0xc0}), document, &error, limits) == false && error.Code == DataErrorCode::InvalidLimits, "invalid limits");
    // イベント順序とsticky failure。
    for (unsigned scenario = 0; scenario < 9; ++scenario)
    {
        MessagePackWriter writer; output = "preserved";
        bool rejected = false;
        switch (scenario)
        {
        case 0: rejected = writer.WriteKey("x") == false; break;
        case 1: rejected = writer.EndArray() == false; break;
        case 2: writer.BeginObject(); rejected = writer.WriteNull() == false; break;
        case 3: writer.BeginObject(); writer.WriteKey("x"); rejected = writer.EndObject() == false; break;
        case 4: writer.BeginArray(); rejected = writer.EndObject() == false; break;
        case 5: writer.WriteNull(); rejected = writer.WriteNull() == false; break;
        case 6: rejected = writer.WriteString(Bytes({255})) == false; break;
        case 7: rejected = writer.WriteFloat64(std::numeric_limits<double>::infinity()) == false; break;
        case 8: writer.BeginObject(); writer.WriteKey("x"); writer.WriteNull(); writer.WriteKey("x"); writer.WriteNull(); rejected = writer.EndObject() == false; break;
        }
        Check(rejected && writer.Finish(output) == false && output == "preserved" && writer.WriteNull() == false, "sticky writer failure");
    }
    DataWriteLimits writeLimits; writeLimits.MaxDepth = 1;
    MessagePackWriter depthWriter(writeLimits);
    Check(depthWriter.BeginArray() && depthWriter.BeginArray() == false, "writer depth");
    writeLimits = {}; writeLimits.MaxContainerElements = 1;
    MessagePackWriter countWriter(writeLimits);
    Check(countWriter.BeginArray() && countWriter.WriteNull() && countWriter.WriteNull() == false, "writer count");
    writeLimits = {}; writeLimits.MaxOutputBytes = 17;
    MessagePackWriter headerWriter(writeLimits);
    Check(headerWriter.BeginArray(), "header limit begin");
    for (int i = 0; i < 16; ++i) { Check(headerWriter.WriteNull(), "header limit element"); }
    Check(headerWriter.EndArray() == false && headerWriter.GetError().Code == DataErrorCode::LimitExceeded, "header growth charged");
    MessagePackWriter incomplete; output = "preserved";
    Check(incomplete.BeginArray() && incomplete.Finish(output) == false && output == "preserved", "unfinished output");
    MessagePackWriter finished;
    Check(finished.WriteNull() && finished.Finish(output) && finished.Finish(output) == false, "finish once");
#ifdef RAVEN_MESSAGEPACK_ALLOCATION_TESTS
    // ParseとWriterそれぞれの全確保位置を失敗させ、例外漏れと部分Commitを検出します。
    const std::string allocationInput = Bytes({0x81,0xa1,'k',0xd9,80}) + std::string(80,'x');
    bool completed = false;
    for (int failAt = 0; failAt < 300; ++failAt)
    {
        Check(MessagePackParser::Parse(Bytes({0xc0}), document), "allocation baseline");
        allocationFailed = false;
        allocationsUntilFailure = failAt;
        const bool success = MessagePackParser::Parse(allocationInput, document, &error);
        allocationsUntilFailure = -1;
        if (allocationFailed == false) { Check(success, "parse allocation sweep completes"); completed = true; break; }
        Check(success == false && error.Code == DataErrorCode::OutOfMemory
            && document.GetRoot().IsType(DataType::Null), "parse allocation failure preserves document");
    }
    Check(completed, "all parser allocations injected");
    Check(MessagePackParser::Parse(allocationInput, document), "writer allocation source");
    completed = false;
    for (int failAt = 0; failAt < 300; ++failAt)
    {
        output = "preserved";
        allocationFailed = false;
        allocationsUntilFailure = failAt;
        const bool success = MessagePackWriter::Write(document, output, &error);
        allocationsUntilFailure = -1;
        if (allocationFailed == false) { Check(success, "writer allocation sweep completes"); completed = true; break; }
        Check(success == false && error.Code == DataErrorCode::OutOfMemory
            && output == "preserved", "writer allocation failure preserves output");
    }
    Check(completed, "all writer allocations injected");
#endif
    std::cout << "MessagePack: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}
#ifdef RAVEN_MESSAGEPACK_TEST_MAIN
// 独立実装との相互検証用。通常のテストmainと別macroにしてVSアプリへ入口を増やしません。
int main() { return Raven::Data::Tests::RunMessagePackTests(); }
#endif

#ifdef RAVEN_MESSAGEPACK_INTEROP_MAIN
#include <iterator>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    Raven::Data::DataDocument document;
    std::string output;
    if (Raven::Data::MessagePackParser::Parse(input, document) == false
        || Raven::Data::MessagePackWriter::Write(document, output) == false) { return 1; }
    std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
    return std::cout.good() ? 0 : 1;
}
#endif
