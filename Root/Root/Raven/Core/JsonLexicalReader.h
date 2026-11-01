#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace Raven
{
namespace Core
{
namespace Detail
{
// 既存DOM ParserとToken Parserで共有する字句規則です。Document型へ依存しません。
enum class JsonLexicalErrorCode { Syntax, Encoding, Limit };
struct JsonLexicalError
{
    JsonLexicalErrorCode Code = JsonLexicalErrorCode::Syntax;
    std::size_t Offset = 0;
    const char* Message = "JSONの構文が不正です";
};
struct JsonStringInfo
{
    std::size_t Begin = 0; // 引用符を除いたRaw範囲
    std::size_t Length = 0;
    std::size_t DecodedLength = 0;
    bool HasEscape = false;
};
struct JsonNumberInfo
{
    std::size_t Begin = 0;
    std::size_t Length = 0;
    bool Floating = false;
};

// output == nullptrなら文字列を所有せず、検証と展開後Byte数の計測だけを行います。
// strictUtf8=falseは既存Parserの生バイト受理規則を維持するための互換経路です。
class JsonLexicalReader
{
public:
    static bool ReadString(std::string_view text, std::size_t& position, JsonStringInfo& info,
        std::string* output, JsonLexicalError& error, bool strictUtf8 = false,
        std::size_t maxRawBytes = (std::numeric_limits<std::size_t>::max)(),
        std::size_t maxDecodedBytes = (std::numeric_limits<std::size_t>::max)());
    // 引用符を除いたJSON String内容を検証・展開します。error.Offsetはraw内の位置です。
    static bool DecodeString(std::string_view raw, std::string* output,
        std::size_t& decodedLength, JsonLexicalError& error, bool strictUtf8 = true,
        std::size_t maxDecodedBytes = (std::numeric_limits<std::size_t>::max)());
    static bool ReadNumber(std::string_view text, std::size_t& position,
        JsonNumberInfo& info, JsonLexicalError& error);
};
}
}
}
