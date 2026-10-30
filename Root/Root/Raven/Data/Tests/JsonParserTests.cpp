#include "Raven/Data/Tests/JsonParserTests.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Core/JsonParser.h"
#include "Raven/Core/JsonLexicalReader.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>

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
void Check(bool condition, const char* message)
{
    ++checks;
    if (condition == false)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
// テスト側でRaw表記を共有字句Readerへ渡し、意味が既存DOMと一致するか再帰比較します。
// この小さなFixture比較の深さは固定。Parser本体は反復Stackで処理します。
bool Equal(const Core::JsonValue& value, DataElement element)
{
    const DataToken* token = element.GetToken();
    if (token == nullptr) { return false; }
    std::string_view raw;
    if (element.TryGetSource(raw) == false) { return false; }
    switch (value.GetType())
    {
    case Core::JsonValue::Type::Null: return token->Type == DataType::Null;
    case Core::JsonValue::Type::Boolean:
        return token->Type == DataType::Bool && token->Payload == (value.GetBoolean() ? 1u : 0u);
    case Core::JsonValue::Type::Number:
    {
        double number = 0;
        if (token->Type == DataType::Float64) { std::memcpy(&number, &token->Payload, sizeof(number)); }
        else if (token->Type == DataType::UInt64) { number = static_cast<double>(token->Payload); }
        else if (token->Type == DataType::Int64)
        {
            std::int64_t integer = 0;
            std::memcpy(&integer, &token->Payload, sizeof(integer));
            number = static_cast<double>(integer);
        }
        else { return false; }
        return number == value.GetNumber();
    }
    case Core::JsonValue::Type::String:
    {
        if (token->Type != DataType::String) { return false; }
        if (token->Payload == DecodedUtf8String) { return raw == value.GetString(); }
        std::string decoded;
        std::size_t length = 0;
        Core::Detail::JsonLexicalError error;
        return Core::Detail::JsonLexicalReader::DecodeString(raw, &decoded, length, error)
            && decoded == value.GetString();
    }
    case Core::JsonValue::Type::Array:
    {
        if (token->Type != DataType::Array || token->Count != value.GetArray().size()) { return false; }
        auto child = element.GetFirstChild();
        for (const auto& item : value.GetArray())
        {
            if (Equal(item, child) == false) { return false; }
            child = child.GetNextSibling();
        }
        return child.IsValid() == false;
    }
    case Core::JsonValue::Type::Object:
    {
        if (token->Type != DataType::Object || token->Count != value.GetObject().size()) { return false; }
        for (auto key = element.GetFirstChild(); key.IsValid();)
        {
            std::string_view keyRaw;
            if (key.TryGetSource(keyRaw) == false) { return false; }
            std::string decoded;
            if (key.GetToken()->Payload == JsonEscapedString)
            {
                Core::Detail::JsonLexicalError error;
                std::size_t length = 0;
                if (Core::Detail::JsonLexicalReader::DecodeString(keyRaw, &decoded, length, error) == false) { return false; }
            }
            else { decoded.assign(keyRaw); }
            const auto* field = value.Find(decoded);
            const auto next = key.GetNextSibling();
            if (field == nullptr || Equal(*field, next) == false) { return false; }
            key = next.GetNextSibling();
        }
        return true;
    }
    }
    return false;
}
}

int RunJsonParserTests()
{
    checks = 0;
    failures = 0;
    const std::vector<std::pair<std::string, std::string>> strings{
        {R"(\"\\\/\b\f\n\r\t)", std::string("\"\\/\b\f\n\r\t")},
        {R"(\u0000)", std::string(1, '\0')}, {R"(\u0061)", "a"},
        {R"(\u3042)", "あ"}, {R"(\uD83D\uDC26)", "🐦"}, {"あ🐦", "あ🐦"}
    };
    for (const auto& item : strings)
    {
        Core::Detail::JsonLexicalError lexicalError;
        std::size_t decodedLength = 0;
        std::string decoded;
        Check(Core::Detail::JsonLexicalReader::DecodeString(item.first, &decoded, decodedLength, lexicalError)
            && decoded == item.second && decodedLength == item.second.size(), "shared String decoder matches independent expected bytes");
        Check(Core::Detail::JsonLexicalReader::DecodeString(item.first, nullptr, decodedLength, lexicalError)
            && decodedLength == item.second.size(), "validation without output measures decoded Byte length");
    }
    Core::Detail::JsonLexicalError lexicalError;
    std::size_t decodedLength = 0;
    Check(Core::Detail::JsonLexicalReader::DecodeString(R"(\u3042)", nullptr, decodedLength, lexicalError, true, 2) == false
        && lexicalError.Code == Core::Detail::JsonLexicalErrorCode::Limit, "decoded Byte upper bound checked without allocating");
    const std::vector<std::string> valid{
        "null", "true", "false", "0", "-0", "42", "-123", "1.25", "1e2", "1E-2", "-0.0", "1e+2",
        "1.7976931348623157e308", "2.2250738585072014e-308", "\"\"", "\"あ🐦\"",
        R"("\"\\\/\b\f\n\r\t")", R"("\u0000\u0061\u3042\uD83D\uDC26")",
        "[]", "{}", "[[],{},true,null,1,\"x\"]", " \r\n {\"a\": [1, {\"b\": false}], \"c\":null} \t ",
        R"({"\u0061":1,"b\n":"\u3042"})", R"({"a":{},"b":{"a":2}})",
        R"({"":0,"\u0000":1})", R"({"\\u0061":0,"a":1})"
    };
    for (const auto& text : valid)
    {
        Core::JsonValue legacy;
        DataDocument document;
        DataError error;
        const bool oldResult = Core::JsonParser::Parse(text, legacy);
        const bool newResult = JsonParser::Parse(text, document, &error);
        Check(oldResult && newResult && Equal(legacy, document.GetRoot()), "common valid Fixture has equal meaning");
        Check(newResult && document.GetSource() == text && error.Code == DataErrorCode::None,
            "Document owns original JSON and clears error");
    }
    const std::vector<std::string> invalid{
        "", " ", "n", "nul", "Null", "TRUE", "truefalse", "null null", "[", "{", "[1", "{\"a\"",
        "[1,]", "[,1]", "[1 2]", "[1,,2]", "{\"a\":1,}", "{1:2}", "{\"a\" 1}",
        "{\"a\":}", "{\"a\":1 \"b\":2}", "{\"a\":1,\"a\":2}", R"({"a":1,"\u0061":2})",
        R"({"\uD83D\uDC26":1,"🐦":2})", "\"abc", R"("\q")", R"("\u12")", R"("\uXY00")",
        R"("\uD800")", R"("\uDC00")", R"("\uD800\u0041")", std::string("\"a\nb\""),
        "00", "01", "-01", "+1", ".1", "1.", "1e", "1e+", "1e-", "--1", "NaN", "Infinity",
        "1e9999", "-1e9999", "1e-9999", "\xEF\xBB\xBF{}", std::string("null\0", 5), "/*x*/null"
    };
    for (const auto& text : invalid)
    {
        Core::JsonValue legacy;
        DataDocument document;
        Check(JsonParser::Parse("true", document), "failure preservation setup");
        const auto oldRoot = document.GetRoot();
        const auto* oldToken = oldRoot.GetToken();
        DataError error;
        Check(Core::JsonParser::Parse(text, legacy) == false && JsonParser::Parse(text, document, &error) == false,
            "common invalid Fixture rejected by both Parsers");
        Check(document.GetSource() == "true" && oldRoot.GetToken() == oldToken
            && error.Code != DataErrorCode::None && error.Offset <= text.size(), "failure preserves output and reports input position");
    }
    DataDocument document;
    DataError error;
    Check(JsonParser::Parse("18446744073709551615", document)
        && document.GetRoot().GetToken()->Type == DataType::UInt64
        && document.GetRoot().GetToken()->Payload == (std::numeric_limits<std::uint64_t>::max)(), "UInt64 max is exact");
    Check(JsonParser::Parse("-9223372036854775808", document), "Int64 min accepted");
    std::int64_t integer = 0;
    const auto* integerToken = document.GetRoot().GetToken();
    if (integerToken != nullptr) { std::memcpy(&integer, &integerToken->Payload, sizeof(integer)); }
    Check(integerToken != nullptr && integerToken->Type == DataType::Int64
        && integer == (std::numeric_limits<std::int64_t>::min)(), "Int64 min bits are exact");
    for (const auto* text : {"18446744073709551616", "-9223372036854775809"})
    {
        Core::JsonValue legacy;
        Check(Core::JsonParser::Parse(text, legacy) && JsonParser::Parse(text, document, &error) == false
            && error.Code == DataErrorCode::NumericRange && error.Offset == 0, "integer range difference is explicit");
    }
    Check(JsonParser::Parse("9007199254740993", document)
        && document.GetRoot().GetToken()->Payload == 9007199254740993ull, "integer beyond double precision is exact");
    Check(JsonParser::Parse("-0.0", document), "negative floating zero accepted");
    double number = 1;
    const auto* numberToken = document.GetRoot().GetToken();
    if (numberToken != nullptr) { std::memcpy(&number, &numberToken->Payload, sizeof(number)); }
    Check(numberToken != nullptr && numberToken->Type == DataType::Float64 && number == 0 && std::signbit(number),
        "negative floating zero sign preserved");
    Check(JsonParser::Parse("1e-320", document), "representable subnormal is accepted by locale independent conversion");
    for (const std::string& raw : {std::string("\x80"), std::string("\xC0\xAF"), std::string("\xED\xA0\x80"),
        std::string("\xF4\x90\x80\x80"), std::string("\xE2\x82"), std::string("\xE2\x41\x80")})
    {
        const std::string text = "\"" + raw + "\"";
        Core::JsonValue legacy;
        Check(Core::JsonParser::Parse(text, legacy) && JsonParser::Parse(text, document, &error) == false
            && error.Code == DataErrorCode::InvalidEncoding, "strict UTF-8 difference is explicit");
    }
    Check(JsonParser::Parse(R"("a\nb")", document), "escaped String parse");
    std::string_view raw;
    Check(document.GetRoot().TryGetSource(raw) && raw == R"(a\nb)"
        && document.GetRoot().GetToken()->Payload == JsonEscapedString, "Raw String retained without eager expansion");
    Check(JsonParser::Parse("true", document), "position setup");
    Check(JsonParser::Parse("[1,]", document, &error) == false && error.Offset == 3
        && error.Code == DataErrorCode::InvalidSyntax, "syntax byte position");
    Check(JsonParser::Parse("[1", document, &error) == false && error.Offset == 2
        && error.Code == DataErrorCode::UnexpectedEnd, "EOF byte position");
    Check(JsonParser::Parse("null x", document, &error) == false && error.Offset == 5
        && error.Code == DataErrorCode::TrailingData, "trailing byte position");
    Check(JsonParser::Parse(R"({"a":1,"\u0061":2})", document, &error) == false
        && error.Offset == 8 && error.Code == DataErrorCode::DuplicateKey, "duplicate reports second decoded key position");
    Check(JsonParser::Parse(R"("\q")", document, &error) == false && error.Offset == 2, "Escape error byte position");

    auto LimitCase = [&](std::string_view text, const DataLimits& limits, bool accepted)
    {
        const std::string before(document.GetSource());
        const bool result = JsonParser::Parse(text, document, &error, limits);
        Check(result == accepted && (result || error.Code == DataErrorCode::LimitExceeded), "limit acceptance and error kind");
        if (result == false) { Check(document.GetSource() == before, "limit failure leaves output unchanged"); }
    };
    DataLimits limits;
    limits.MaxInputBytes = 4;
    LimitCase("null", limits, true); LimitCase(" null", limits, false);
    limits = {}; limits.MaxTokens = 3;
    LimitCase("[0,1]", limits, true); LimitCase("[0,1,2]", limits, false);
    limits.MaxTokens = 2;
    LimitCase("{\"a\":0}", limits, false); // ObjectキーもTokenへ含めます。
    limits = {}; limits.MaxDepth = 1;
    LimitCase("[]", limits, true); LimitCase("[[]]", limits, false);
    limits = {}; limits.MaxDepth = 0;
    LimitCase("null", limits, true); LimitCase("{}", limits, false);
    limits = {}; limits.MaxValueBytes = 6;
    LimitCase(R"("\u3042")", limits, true); LimitCase(R"("\u3042x")", limits, false);
    limits.MaxValueBytes = 0;
    LimitCase("\"\"", limits, true); LimitCase("\"a\"", limits, false); LimitCase("{\"a\":0}", limits, false);
    limits = {}; limits.MaxContainerElements = 1;
    LimitCase("[0]", limits, true); LimitCase("[0,1]", limits, false);
    LimitCase("{\"a\":0}", limits, true); LimitCase("{\"a\":0,\"b\":1}", limits, false);
    limits = {}; limits.MaxTokens = 0;
    LimitCase("null", limits, false);
    limits = {}; limits.MaxTokens = InvalidTokenId;
    Check(JsonParser::Parse("null", document, &error, limits) == false && error.Code == DataErrorCode::InvalidLimits,
        "invalid Limits rejected before parse");
    std::string large = "[0";
    for (int i = 1; i < 65536; ++i) { large += ",0"; }
    large += "]";
    Check(JsonParser::Parse(large, document) && document.GetRoot().GetToken()->Count == 65536,
        "direct parse supports more than 65535 Array values");
    std::string deep(128, '['); deep += "0"; deep.append(128, ']');
    Check(JsonParser::Parse(deep, document), "default depth boundary accepted");
    deep = '[' + deep + ']';
    Check(JsonParser::Parse(deep, document, &error) == false && error.Code == DataErrorCode::LimitExceeded,
        "default depth boundary overflow rejected");
    limits = {}; limits.MaxDepth = 2000;
    deep.assign(2000, '['); deep += '0'; deep.append(2000, ']');
    Check(JsonParser::Parse(deep, document, &error, limits), "explicit large depth uses no recursive Call Stack");
    Check(JsonParser::Parse("[1,2]", document), "alias setup");
    Check(JsonParser::Parse(document.GetSource(), document), "input may alias output Source");
    Check(document.GetRoot().GetToken()->Count == 2 && document.GetSource() == "[1,2]", "aliased input remains owned");
    std::cout << "JsonParser: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}

#ifdef RAVEN_JSON_TEST_MAIN
int main()
{
    return Raven::Data::Tests::RunJsonParserTests();
}
#endif
