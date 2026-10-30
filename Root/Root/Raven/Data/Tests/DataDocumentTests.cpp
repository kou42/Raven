#include "Raven/Data/Tests/DataDocumentTests.h"
#include "Raven/Data/DataDocument.h"
#include "Raven/Core/JsonValue.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

namespace Raven
{
namespace Data
{
namespace Tests
{
namespace
{
int failures = 0;
int checks = 0;
void Check(bool condition, const char* description)
{
    ++checks;
    if (condition == false)
    {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}
DataToken Token(DataType type, TokenId next, std::uint32_t count = 0,
    std::uint32_t offset = 0, std::uint32_t length = 0, std::uint64_t payload = 0)
{
    return {type, offset, length, next, count, payload};
}
}

int RunDataDocumentTests()
{
    failures = 0;
    checks = 0;
    static_assert(std::is_copy_constructible<DataDocument>::value == false, "Document must own uniquely");
    static_assert(std::is_nothrow_move_constructible<DataDocument>::value, "Move must not allocate");
    DataDocument document;
    DataError error;
    Check(document.GetRoot().IsValid() == false, "default Document has no root");
    Check(DataElement{}.GetId() == InvalidTokenId, "invalid TokenId is reserved");
    std::string_view preserved = "unchanged";
    Check(DataElement{}.TryGetSource(preserved) == false && preserved == "unchanged",
        "invalid access leaves output unchanged");

    // {"a":[null,{"":null}],"b":true}。Objectキーと値は直下の別Tokenです。
    std::vector<DataToken> tape{
        Token(DataType::Object, 9, 2), Token(DataType::String, 2, 0, 0, 1),
        Token(DataType::Array, 7, 2), Token(DataType::Null, 4), Token(DataType::Object, 7, 1),
        Token(DataType::String, 6), Token(DataType::Null, 7),
        Token(DataType::String, 8, 0, 1, 1), Token(DataType::Bool, 9, 0, 0, 0, 1)};
    std::string source = "ab";
    Check(DataDocument::Create(source, tape, document, &error), "nested tape accepted");
    source[0] = 'x';
    tape[0].Next = 0;
    auto root = document.GetRoot();
    auto key = root.GetFirstChild();
    Check(key.TryGetSource(preserved) && preserved == "a", "Document owns source copy");
    auto array = key.GetNextSibling();
    Check(array.GetToken() != nullptr && array.GetToken()->Type == DataType::Array, "Document owns tape copy");
    auto lastKey = array.GetNextSibling();
    Check(lastKey.TryGetSource(preserved) && preserved == "b", "Skip nested subtree to sibling");
    Check(root.GetNextSibling().IsValid() == false, "Root has no sibling");
    Check(array.GetFirstChild().GetNextSibling().GetNextSibling().IsValid() == false,
        "children cannot escape parent boundary");
    Check(lastKey.GetFirstChild().IsValid() == false, "scalar has no children");
    const auto oldRoot = document.GetRoot();
    Check(DataDocument::Create("", {}, document, &error) == false
        && error.Code == DataErrorCode::InvalidStructure && oldRoot.IsValid()
        && document.GetTokenCount() == 9, "failure preserves Document and views");

    auto Reject = [&](const std::vector<DataToken>& invalid, DataErrorCode expected,
        const DataLimits& limits = DataLimits{}, std::string_view bytes = "")
    {
        const bool result = DataDocument::Create(bytes, invalid, document, &error, limits);
        Check(result == false && error.Code == expected && document.GetTokenCount() == 9,
            "reject invalid tape without changing output");
    };
    Reject({Token(DataType::Null, InvalidTokenId)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Null, 0)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Null, 2), Token(DataType::Null, 2)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Array, 2, 0), Token(DataType::Null, 2)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Array, 1, 1)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Object, 2, 1), Token(DataType::Null, 2)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Bool, 1, 0, 0, 0, 2)}, DataErrorCode::InvalidStructure);
    Reject({Token(static_cast<DataType>(123), 1)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::String, 1, 0, 0xFFFFFFFEu, 8)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::String, 1, 0, 0, 0xFFFFFFFFu)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Null, 1, 1)}, DataErrorCode::InvalidStructure);
    Reject({Token(DataType::Array, 3, 1), Token(DataType::Array, 3, 0), Token(DataType::Null, 3)},
        DataErrorCode::InvalidStructure);
    std::vector<DataToken> duplicate{Token(DataType::Object, 5, 2),
        Token(DataType::String, 2, 0, 0, 1), Token(DataType::Null, 3),
        Token(DataType::String, 4, 0, 1, 1), Token(DataType::Null, 5)};
    Reject(duplicate, DataErrorCode::DuplicateKey, {}, "aa");
    for (std::string_view bad : {std::string_view("\x80", 1), std::string_view("\xC0\xAF", 2),
        std::string_view("\xED\xA0\x80", 3), std::string_view("\xF4\x90\x80\x80", 4),
        std::string_view("\xE2\x82", 2)})
    {
        Reject({Token(DataType::String, 1, 0, 0, static_cast<std::uint32_t>(bad.size()))},
            DataErrorCode::InvalidEncoding, {}, bad);
    }
    std::uint64_t nanBits = 0;
    const double nan = (std::numeric_limits<double>::quiet_NaN)();
    std::memcpy(&nanBits, &nan, sizeof(nan));
    Reject({Token(DataType::Float64, 1, 0, 0, 0, nanBits)}, DataErrorCode::NumericRange);
    DataLimits limits;
    limits.MaxTokens = InvalidTokenId;
    Reject({Token(DataType::Null, 1)}, DataErrorCode::InvalidLimits, limits);
    limits = {}; limits.MaxTokens = 0;
    Reject({Token(DataType::Null, 1)}, DataErrorCode::LimitExceeded, limits);
    limits = {}; limits.MaxInputBytes = 0;
    Reject({Token(DataType::Null, 1)}, DataErrorCode::LimitExceeded, limits, "x");
    limits = {}; limits.MaxValueBytes = 1;
    Reject({Token(DataType::String, 1, 0, 0, 2)}, DataErrorCode::LimitExceeded, limits, "ab");
    limits = {}; limits.MaxContainerElements = 0;
    Reject({Token(DataType::Array, 2, 1), Token(DataType::Null, 2)}, DataErrorCode::LimitExceeded, limits);
    limits = {}; limits.MaxDepth = 0;
    Reject({Token(DataType::Array, 1)}, DataErrorCode::LimitExceeded, limits);
    limits = {}; limits.MaxDepth = 1;
    Reject({Token(DataType::Array, 2, 1), Token(DataType::Array, 2)}, DataErrorCode::LimitExceeded, limits);

    DataDocument boundary;
    limits = {}; limits.MaxInputBytes = 1; limits.MaxValueBytes = 1; limits.MaxTokens = 1;
    Check(DataDocument::Create("a", {Token(DataType::String, 1, 0, 0, 1)}, boundary, &error, limits),
        "exact size limits accepted");
    Check(DataDocument::Create("", {Token(DataType::Array, 1)}, boundary), "empty Array accepted");
    Check(boundary.GetRoot().GetFirstChild().IsValid() == false, "empty Array has no children");
    Check(DataDocument::Create("", {Token(DataType::Object, 1)}, boundary), "empty Object accepted");
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    Check(DataDocument::Create("", {Token(DataType::UInt64, 1, 0, 0, 0, maximum)}, boundary)
        && boundary.GetRoot().GetToken()->Payload == maximum, "UInt64 max preserved without double conversion");
    const std::int64_t minimum = (std::numeric_limits<std::int64_t>::min)();
    std::uint64_t minimumBits = 0;
    std::memcpy(&minimumBits, &minimum, sizeof(minimum));
    Check(DataDocument::Create("", {Token(DataType::Int64, 1, 0, 0, 0, minimumBits)}, boundary)
        && boundary.GetRoot().GetToken()->Payload == minimumBits, "Int64 min bit representation preserved");
    const std::string unicode("\0\xE3\x81\x82\xF0\x9F\x90\xA6", 8);
    Check(DataDocument::Create(unicode, {Token(DataType::String, 1, 0, 0, 8)}, boundary),
        "UTF-8 and embedded NUL accepted");
    Check(boundary.GetRoot().TryGetSource(preserved) && preserved.size() == 8, "source is length aware");
    Check(DataDocument::Create("\xFF", {Token(DataType::Binary, 1, 0, 0, 1)}, boundary),
        "Binary does not require UTF-8");

    constexpr std::uint32_t largeCount = 65536;
    std::vector<DataToken> large{Token(DataType::Array, largeCount + 1u, largeCount)};
    for (std::uint32_t i = 1; i <= largeCount; ++i)
    {
        large.push_back(Token(DataType::UInt64, i + 1u, 0, 0, 0, i));
    }
    Check(DataDocument::Create("", large, boundary), "more than 65535 elements accepted");
    std::uint32_t visited = 0;
    for (auto child = boundary.GetRoot().GetFirstChild(); child.IsValid(); child = child.GetNextSibling())
    {
        ++visited;
    }
    Check(visited == largeCount, "large Array traversal preserves full count");
    DataDocument moved(std::move(boundary));
    Check(boundary.GetRoot().IsValid() == false && moved.GetTokenCount() == largeCount + 1u,
        "Move transfers ownership and empties source");
    boundary = std::move(moved);
    Check(moved.GetRoot().IsValid() == false && boundary.GetRoot().IsValid(), "Move assignment transfers ownership");

    Core::JsonValue::Object object;
    object.emplace("z", Core::JsonValue(true));
    object.emplace("a", Core::JsonValue(Core::JsonValue::Array{Core::JsonValue(9007199254740992.0),
        Core::JsonValue(unicode), Core::JsonValue{}}));
    const Core::JsonValue json(std::move(object));
    Check(DataDocument::FromJsonValue(json, boundary, &error), "JsonValue Adapter accepts nested values");
    auto firstKey = boundary.GetRoot().GetFirstChild();
    Check(firstKey.TryGetSource(preserved) && preserved == "a", "Adapter sorts Object keys");
    const auto number = firstKey.GetNextSibling().GetFirstChild().GetToken();
    double decoded = 0;
    if (number != nullptr)
    {
        std::memcpy(&decoded, &number->Payload, sizeof(decoded));
    }
    Check(number != nullptr && number->Type == DataType::Float64 && decoded == 9007199254740992.0,
        "Adapter preserves existing double value and type");
    const auto beforeCount = boundary.GetTokenCount();
    Check(DataDocument::FromJsonValue(Core::JsonValue(nan), boundary, &error) == false
        && error.Code == DataErrorCode::NumericRange && boundary.GetTokenCount() == beforeCount,
        "Adapter rejects nonfinite without modifying output");
    Check(DataDocument::FromJsonValue(Core::JsonValue(std::string("\xFF")), boundary, &error) == false
        && error.Code == DataErrorCode::InvalidEncoding, "Adapter rejects invalid UTF-8");
    limits = {}; limits.MaxDepth = 1;
    Check(DataDocument::FromJsonValue(json, boundary, &error, limits) == false
        && error.Code == DataErrorCode::LimitExceeded, "Adapter checks depth before descending");
    limits = {}; limits.MaxTokens = 2;
    Check(DataDocument::FromJsonValue(json, boundary, &error, limits) == false
        && error.Code == DataErrorCode::LimitExceeded, "Adapter checks child count before queue allocation");
    limits = {}; limits.MaxInputBytes = 1;
    Check(DataDocument::FromJsonValue(json, boundary, &error, limits) == false
        && error.Code == DataErrorCode::LimitExceeded, "Adapter checks accumulated string bytes");
    // 深さはOpenコンテナ数で数えます。再帰呼び出しを使わないため、
    // 手動Tapeの上限拡大もC++のCall Stack容量へ依存しません。
    std::vector<DataToken> deep;
    for (std::uint32_t i = 0; i < 128; ++i)
    {
        deep.push_back(Token(DataType::Array, 129, 1));
    }
    deep.push_back(Token(DataType::Null, 129));
    Check(DataDocument::Create("", deep, boundary, &error), "depth exactly 128 accepted");
    deep.insert(deep.begin(), Token(DataType::Array, 130, 1));
    for (auto& token : deep)
    {
        token.Next = 130;
    }
    Check(DataDocument::Create("", deep, boundary, &error) == false
        && error.Code == DataErrorCode::LimitExceeded, "depth 129 rejected by default");
    limits = {}; limits.MaxDepth = 129;
    Check(DataDocument::Create("", deep, boundary, &error, limits), "explicit depth increase accepted");
    limits = {}; limits.MaxContainerElements = 2; limits.MaxTokens = 3;
    Check(DataDocument::Create("", {Token(DataType::Array, 3, 2), Token(DataType::Null, 2),
        Token(DataType::Null, 3)}, boundary, &error, limits), "container and Token limits inclusive");
    const Core::JsonValue emptyArray(Core::JsonValue::Array{});
    limits = {}; limits.MaxDepth = 1; limits.MaxTokens = 1; limits.MaxContainerElements = 0;
    Check(DataDocument::FromJsonValue(emptyArray, boundary, &error, limits),
        "Adapter accepts empty container at exact limits");
    limits.MaxDepth = 0;
    Check(DataDocument::FromJsonValue(emptyArray, boundary, &error, limits) == false,
        "Adapter counts empty Root container toward depth");
    const Core::JsonValue zero(-0.0);
    Check(DataDocument::FromJsonValue(zero, boundary, &error), "Adapter accepts negative zero");
    const auto zeroToken = boundary.GetRoot().GetToken();
    double zeroValue = 1;
    if (zeroToken != nullptr)
    {
        std::memcpy(&zeroValue, &zeroToken->Payload, sizeof(zeroValue));
    }
    Check(zeroToken != nullptr && zeroValue == 0.0 && std::signbit(zeroValue), "negative zero sign preserved");
    // 同一Document内部のSourceを入力にしても、Commitまでは参照を失いません。
    Check(DataDocument::Create("abc", {Token(DataType::String, 1, 0, 0, 3)}, boundary), "alias setup");
    Check(DataDocument::Create(boundary.GetSource(), {Token(DataType::String, 1, 0, 1, 2)}, boundary),
        "Create permits Source alias with output");
    Check(boundary.GetRoot().TryGetSource(preserved) && preserved == "bc", "aliased Source copied before Commit");

    std::cout << "DataDocument: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}

#ifdef RAVEN_DATA_TEST_MAIN
int main()
{
    return Raven::Data::Tests::RunDataDocumentTests();
}
#endif
