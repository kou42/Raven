#include "Raven/Data/Tests/DataElementTests.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Core/JsonValue.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <thread>
#include <vector>

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
}

int RunDataElementTests()
{
    checks = 0;
    failures = 0;
    DataElement invalid;
    DataType type = DataType::Binary;
    Check(invalid.IsType(DataType::Null) == false, "invalid Element is not Null");
    Check(invalid.TryGetType(type) == false && type == DataType::Binary, "invalid type lookup preserves output");
    bool boolean = true;
    std::int64_t signedValue = 123;
    std::uint64_t unsignedValue = 456;
    double floatingValue = 789;
    std::uint32_t count = 42;
    std::string decoded = "preserved";
    std::string_view view = "preserved";
    Check(invalid.TryGetBool(boolean) == false && boolean, "invalid Bool lookup preserves output");
    Check(invalid.TryGetInt64(signedValue) == false && signedValue == 123, "invalid Int64 lookup preserves output");
    Check(invalid.TryGetUInt64(unsignedValue) == false && unsignedValue == 456, "invalid UInt64 lookup preserves output");
    Check(invalid.TryGetFloat64(floatingValue) == false && floatingValue == 789, "invalid Float64 lookup preserves output");
    Check(invalid.TryGetCount(count) == false && count == 42, "invalid Count lookup preserves output");
    Check(invalid.TryGetRawString(view) == false && view == "preserved", "invalid Raw String preserves output");
    Check(invalid.TryGetStringView(view) == false && view == "preserved", "invalid String View preserves output");
    Check(invalid.TryGetDecodedString(decoded) == false && decoded == "preserved", "invalid Decoded String preserves output");

    DataDocument document;
    Check(JsonParser::Parse(R"({"plain":"あ🐦","escaped":"a\nb\u0000\uD83D\uDC26","empty":"","\u0061":7,"nul\u0000key":8,"items":[1,{"nested":2},[3]],"flag":true,"null":null})", document),
        "String and traversal Fixture parses");
    auto root = document.GetRoot();
    Check(root.TryGetType(type) && type == DataType::Object && root.IsType(DataType::Object), "valid type lookup");
    Check(root.TryGetCount(count) && count == 8, "Object Count counts members");
    DataElement plain, escaped, empty, items, flag, null;
    Check(root.TryFind("plain", plain) && root.TryFind("escaped", escaped) && root.TryFind("empty", empty)
        && root.TryFind("items", items) && root.TryFind("flag", flag) && root.TryFind("null", null), "plain keys find values");
    Check(plain.TryGetStringView(view) && view == "あ🐦", "unescaped UTF-8 returns View");
    const auto plainView = view;
    Check(plain.TryGetRawString(view) && view == "あ🐦", "unescaped Raw String is UTF-8");
    Check(plain.TryGetDecodedString(decoded) && decoded == "あ🐦", "unescaped String can be copied into owned output");
    Check(escaped.TryGetRawString(view) && view == R"(a\nb\u0000\uD83D\uDC26)", "escaped Raw String retains original spelling");
    view = "preserved";
    Check(escaped.TryGetStringView(view) == false && view == "preserved", "escaped String cannot expose decoded Source View");
    const std::string expected = std::string("a\nb\0", 4) + "🐦";
    Check(escaped.TryGetDecodedString(decoded) && decoded == expected, "Decoded String expands Escape, NUL, surrogate pair");
    Check(plainView == "あ🐦", "Source View survives unrelated output Buffer changes");
    Check(empty.TryGetDecodedString(decoded) && decoded.empty(), "empty String clears output on success");
    Check(empty.TryGetStringView(view) && view.empty(), "empty String has valid View");
    Check(flag.TryGetBool(boolean) && boolean, "Bool getter reads true");
    Check(null.IsType(DataType::Null) && null.TryGetBool(boolean) == false && boolean, "explicit Null is not converted to Bool");
    decoded = "preserved";
    Check(flag.TryGetDecodedString(decoded) == false && decoded == "preserved", "nonstring cannot be decoded");
    view = "preserved";
    Check(flag.TryGetRawString(view) == false && flag.TryGetStringView(view) == false && view == "preserved",
        "nonstring String APIs preserve output");
    signedValue = 123;
    Check(flag.TryGetInt64(signedValue) == false && signedValue == 123, "Bool is not implicitly Number");
    Check(plain.TryGetUInt64(unsignedValue) == false && unsignedValue == 456, "String is not implicitly Number");
    count = 42;
    Check(plain.TryGetCount(count) == false && count == 42, "scalar does not have Count");

    DataElement found = plain;
    Check(root.TryFind("a", found) && found.TryGetUInt64(unsignedValue) && unsignedValue == 7, "escaped Object key compares decoded text");
    const std::string nulKey("nul\0key", 7);
    Check(root.TryFind(nulKey, found) && found.TryGetUInt64(unsignedValue) && unsignedValue == 8, "key lookup is NUL length aware");
    found = plain;
    Check(root.TryFind("missing", found) == false && found.GetId() == plain.GetId(), "missing key preserves output");
    Check(root.TryFind("Plain", found) == false && found.GetId() == plain.GetId(), "key comparison is case sensitive");
    Check(plain.TryFind("plain", found) == false && found.GetId() == plain.GetId(), "nonobject lookup preserves output");
    Check(invalid.TryFind("a", found) == false && found.GetId() == plain.GetId(), "invalid lookup preserves output");
    Check(items.TryGetCount(count) && count == 3, "Array Count counts values");
    Check(items.TryGetArrayElement(1, found) && found.IsType(DataType::Object), "Array indexing skips preceding subtrees");
    Check(found.TryFind("nested", found) && found.TryGetUInt64(unsignedValue) && unsignedValue == 2,
        "lookup output may alias receiving Element");
    Check(items.TryGetArrayElement(2, found) && found.IsType(DataType::Array), "Array indexing reaches nested Array");
    const auto beforeId = found.GetId();
    Check(items.TryGetArrayElement(3, found) == false && found.GetId() == beforeId, "Array index at Count is rejected");
    Check(items.TryGetArrayElement(InvalidTokenId, found) == false && found.GetId() == beforeId, "huge Array index rejected before walk");
    Check(root.TryGetArrayElement(0, found) == false && found.GetId() == beforeId, "Object cannot be indexed as Array");
    auto aliasedArray = items;
    Check(aliasedArray.TryGetArrayElement(0, aliasedArray) && aliasedArray.TryGetUInt64(unsignedValue) && unsignedValue == 1,
        "Array output may alias receiving Element");
    DataElement key = plain, value = escaped;
    Check(root.TryGetObjectMember(3, key, value) && key.TryGetDecodedString(decoded) && decoded == "a"
        && value.TryGetUInt64(unsignedValue) && unsignedValue == 7, "Object indexed walk yields key and value");
    const auto keyId = key.GetId(), valueId = value.GetId();
    Check(root.TryGetObjectMember(8, key, value) == false && key.GetId() == keyId && value.GetId() == valueId,
        "Object out of range preserves both outputs");
    Check(root.TryGetObjectMember(0, key, key) == false && key.GetId() == keyId, "aliased key and value outputs rejected");
    Check(items.TryGetObjectMember(0, key, value) == false && key.GetId() == keyId && value.GetId() == valueId,
        "Array cannot be indexed as Object");
    std::uint32_t visited = 0;
    for (auto child = items.GetFirstChild(); child.IsValid(); child = child.GetNextSibling()) { ++visited; }
    Check(visited == 3, "Array linear walk skips whole nested values");
    visited = 0;
    for (auto child = root.GetFirstChild(); child.IsValid(); child = child.GetNextSibling().GetNextSibling()) { ++visited; }
    Check(visited == 8, "Object linear walk counts member pairs");

    DataDocument unusualKeys;
    Check(JsonParser::Parse(R"({"":null,"\\u0061":1,"a":2,"\uD83D\uDC26":3,"emptyArray":[],"emptyObject":{}})", unusualKeys),
        "empty, literal Escape and Unicode key Fixture parses");
    const auto unusualRoot = unusualKeys.GetRoot();
    Check(unusualRoot.TryFind("", found) && found.IsType(DataType::Null), "empty Object key can be found");
    Check(unusualRoot.TryFind("\\u0061", found) && found.TryGetUInt64(unsignedValue) && unsignedValue == 1,
        "literal backslash key is decoded exactly once");
    Check(unusualRoot.TryFind("a", found) && found.TryGetUInt64(unsignedValue) && unsignedValue == 2,
        "literal Escape text differs from decoded key");
    Check(unusualRoot.TryFind("🐦", found) && found.TryGetUInt64(unsignedValue) && unsignedValue == 3,
        "surrogate pair key matches UTF-8 query");
    DataElement emptyArray, emptyObject;
    Check(unusualRoot.TryFind("emptyArray", emptyArray) && unusualRoot.TryFind("emptyObject", emptyObject),
        "empty containers can be found");
    Check(emptyArray.TryGetCount(count) && count == 0 && emptyArray.TryGetArrayElement(0, found) == false,
        "empty Array has zero values and no valid index");
    Check(emptyObject.TryGetCount(count) && count == 0 && emptyObject.TryGetObjectMember(0, key, value) == false,
        "empty Object has zero pairs and no valid index");
    auto aliasedObject = root;
    Check(aliasedObject.TryGetObjectMember(0, aliasedObject, found) && aliasedObject.TryGetStringView(view)
        && view == "plain" && found.TryGetStringView(view) && view == "あ🐦",
        "Object key output may alias receiving Element");
    std::string large = "[0";
    for (int i = 1; i < 65536; ++i) { large += ",0"; }
    large += ']';
    DataDocument largeArray;
    Check(JsonParser::Parse(large, largeArray) && largeArray.GetRoot().TryGetCount(count) && count == 65536,
        "large Array Count is not truncated to 16 bit");
    Check(largeArray.GetRoot().TryGetArrayElement(65535, found) && found.TryGetUInt64(unsignedValue)
        && unsignedValue == 0, "large Array final index is accessible");

    struct NumberCase
    {
        const char* Json;
        bool Signed;
        std::int64_t SignedValue;
        bool Unsigned;
        std::uint64_t UnsignedValue;
        bool Float;
        double FloatValue;
    };
    const auto minimum = (std::numeric_limits<std::int64_t>::min)();
    const auto maximum = (std::numeric_limits<std::int64_t>::max)();
    const auto unsignedMaximum = (std::numeric_limits<std::uint64_t>::max)();
    const NumberCase numbers[]{
        {"0", true, 0, true, 0, true, 0}, {"-0", true, 0, true, 0, true, 0},
        {"-1", true, -1, false, 0, true, -1}, {"42", true, 42, true, 42, true, 42},
        {"42.0", true, 42, true, 42, true, 42}, {"-1.0", true, -1, false, 0, true, -1},
        {"1.5", false, 0, false, 0, true, 1.5}, {"-1.5", false, 0, false, 0, true, -1.5},
        {"1e-320", false, 0, false, 0, true, 1e-320},
        {"9007199254740992", true, 9007199254740992ll, true, 9007199254740992ull, true, 0x1p53},
        {"9007199254740993", true, 9007199254740993ll, true, 9007199254740993ull, false, 0},
        {"9007199254740994", true, 9007199254740994ll, true, 9007199254740994ull, true, 9007199254740994.0},
        {"-9007199254740993", true, -9007199254740993ll, false, 0, false, 0},
        {"-9007199254740994", true, -9007199254740994ll, false, 0, true, -9007199254740994.0},
        {"9223372036854775807", true, maximum, true, static_cast<std::uint64_t>(maximum), false, 0},
        {"-9223372036854775808", true, minimum, false, 0, true, -0x1p63},
        {"9223372036854775808", false, 0, true, 9223372036854775808ull, true, 0x1p63},
        {"18446744073709551615", false, 0, true, unsignedMaximum, false, 0},
        {"18446744073709549568", false, 0, true, 18446744073709549568ull, true, 0x1p64 - 2048},
        {"9223372036854775807.0", false, 0, true, 9223372036854775808ull, true, 0x1p63},
        {"-9223372036854775808.0", true, minimum, false, 0, true, -0x1p63},
        {"9223372036854774784.0", true, 9223372036854774784ll, true, 9223372036854774784ull, true, 0x1p63 - 1024},
        {"18446744073709551615.0", false, 0, false, 0, true, 0x1p64},
        {"18446744073709549568.0", false, 0, true, 18446744073709549568ull, true, 0x1p64 - 2048},
        {"-9223372036854777856.0", false, 0, false, 0, true, -0x1p63 - 2048},
        {"1e100", false, 0, false, 0, true, 1e100}
    };
    DataDocument numeric;
    for (const auto& item : numbers)
    {
        Check(JsonParser::Parse(item.Json, numeric), "numeric boundary Fixture parses");
        const auto number = numeric.GetRoot();
        signedValue = 123; unsignedValue = 456; floatingValue = 789; boolean = true;
        const bool signedResult = number.TryGetInt64(signedValue);
        const bool unsignedResult = number.TryGetUInt64(unsignedValue);
        const bool floatResult = number.TryGetFloat64(floatingValue);
        Check(signedResult == item.Signed && signedValue == (item.Signed ? item.SignedValue : 123), "Int64 exact range and unchanged failure output");
        Check(unsignedResult == item.Unsigned && unsignedValue == (item.Unsigned ? item.UnsignedValue : 456), "UInt64 sign, range and unchanged failure output");
        Check(floatResult == item.Float && floatingValue == (item.Float ? item.FloatValue : 789), "Float64 rejects integer precision loss and preserves failure output");
        Check(number.TryGetBool(boolean) == false && boolean, "Number is not implicitly Bool");
    }
    Check(JsonParser::Parse("-0.0", numeric), "negative floating zero Fixture parses");
    Check(numeric.GetRoot().TryGetFloat64(floatingValue) && std::signbit(floatingValue), "Float64 preserves negative zero sign");
    Check(numeric.GetRoot().TryGetInt64(signedValue) && signedValue == 0
        && numeric.GetRoot().TryGetUInt64(unsignedValue) && unsignedValue == 0, "negative zero converts to integer zero");
    Check(JsonParser::Parse("false", numeric) && numeric.GetRoot().TryGetBool(boolean) && boolean == false,
        "Bool getter reads false");
    Check(JsonParser::Parse("\"42\"", numeric), "numeric String Fixture parses");
    Check(numeric.GetRoot().TryGetInt64(signedValue) == false && numeric.GetRoot().TryGetFloat64(floatingValue) == false,
        "numeric String is not parsed implicitly");

    // long doubleが整数64 bitを正確に保持できる環境で独立した精度判定と照合します。
    // doubleと同じ精度のMSVC long doubleでは、上の固定境界Fixtureを使います。
    if constexpr (std::numeric_limits<long double>::digits >= 64)
    {
        std::mt19937_64 random(1030);
        for (int i = 0; i < 512; ++i)
        {
            const auto bits = random();
            const auto trailingZeros = static_cast<unsigned int>(i % 64);
            const auto payload = (bits >> trailingZeros) << trailingZeros;
            DataToken token;
            token.Type = DataType::UInt64; token.Next = 1; token.Payload = payload;
            Check(DataDocument::Create("", {token}, numeric), "random integer Tape creates");
            const bool expectedExact = static_cast<long double>(payload) == static_cast<long double>(static_cast<double>(payload));
            floatingValue = 789;
            const bool result = numeric.GetRoot().TryGetFloat64(floatingValue);
            Check(result == expectedExact && floatingValue == (result ? static_cast<double>(payload) : 789),
                "UInt64 precision check matches independent long double comparison");
            token.Type = DataType::Int64;
            std::int64_t signedPayload = 0;
            std::memcpy(&signedPayload, &payload, sizeof(signedPayload));
            Check(DataDocument::Create("", {token}, numeric), "random signed Tape creates");
            const bool expectedSigned = static_cast<long double>(signedPayload) == static_cast<long double>(static_cast<double>(signedPayload));
            floatingValue = 789;
            const bool signedResult = numeric.GetRoot().TryGetFloat64(floatingValue);
            Check(signedResult == expectedSigned && floatingValue == (signedResult ? static_cast<double>(signedPayload) : 789),
                "Int64 precision check matches independent long double comparison");
        }
    }

    DataDocument adapter;
    const Core::JsonValue source(Core::JsonValue::Object{{"\\literal", Core::JsonValue(std::string("a\\b"))},
        {"n", Core::JsonValue(2.0)}});
    Check(DataDocument::FromJsonValue(source, adapter), "existing DOM Adapter Fixture creates");
    Check(adapter.GetRoot().TryFind("\\literal", found) && found.TryGetStringView(view) && view == "a\\b",
        "decoded Adapter Strings with backslash are not decoded twice");
    Check(found.TryGetDecodedString(decoded) && decoded == "a\\b", "Adapter Decoded getter copies literal backslash");
    DataDocument binary;
    DataToken bytes; bytes.Type = DataType::Binary; bytes.Length = 1; bytes.Next = 1;
    Check(DataDocument::Create("x", {bytes}, binary), "Binary Fixture creates");
    decoded = "preserved";
    Check(binary.GetRoot().IsType(DataType::Binary) && binary.GetRoot().TryGetDecodedString(decoded) == false
        && decoded == "preserved", "Binary does not implicitly become String");
    DataDocument moved(std::move(adapter));
    Check(moved.GetRoot().TryFind("n", found) && found.TryGetInt64(signedValue) && signedValue == 2,
        "reacquired Element after Move reads transferred data");
    // Move前のElement / Viewは使用せず、新しい所有先から取得した参照だけを使います。
    Check(adapter.GetRoot().IsValid() == false, "Move source is empty");

    std::atomic<bool> parallelCorrect{true};
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker)
    {
        threads.emplace_back([&document, &expected, &nulKey, &parallelCorrect]()
        {
            // 出力BufferとElementはWorkerごとに独立させ、Documentは読み取りだけを行います。
            std::string output;
            DataElement element;
            std::uint64_t integerOutput = 0;
            for (int i = 0; i < 500; ++i)
            {
                const auto sharedRoot = document.GetRoot();
                if (sharedRoot.TryFind("escaped", element) == false || element.TryGetDecodedString(output) == false
                    || output != expected || sharedRoot.TryFind(nulKey, element) == false
                    || element.TryGetUInt64(integerOutput) == false || integerOutput != 8)
                {
                    parallelCorrect.store(false);
                    return;
                }
            }
        });
    }
    for (auto& thread : threads) { thread.join(); }
    Check(parallelCorrect.load(), "shared Document supports independent parallel Decode and key lookup");
    std::cout << "DataElement: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}

#ifdef RAVEN_DATA_ELEMENT_TEST_MAIN
int main()
{
    return Raven::Data::Tests::RunDataElementTests();
}
#endif
