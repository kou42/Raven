#include "Raven/Core/JsonLexicalReader.h"
#include <cstdint>

namespace Raven
{
namespace Core
{
namespace Detail
{
namespace
{
bool Fail(JsonLexicalError& error, JsonLexicalErrorCode code, std::size_t offset, const char* message)
{
    error = {code, offset, message};
    return false;
}
bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}
bool ReadHex4(std::string_view raw, std::size_t& position, std::uint32_t& value, JsonLexicalError& error)
{
    if (raw.size() - position < 4)
    {
        return Fail(error, JsonLexicalErrorCode::Syntax, raw.size(), "Unicode Escapeが4桁未満です");
    }
    value = 0;
    for (int i = 0; i < 4; ++i)
    {
        const char c = raw[position++];
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') { digit = static_cast<std::uint32_t>(c - '0'); }
        else if (c >= 'a' && c <= 'f') { digit = static_cast<std::uint32_t>(c - 'a' + 10); }
        else if (c >= 'A' && c <= 'F') { digit = static_cast<std::uint32_t>(c - 'A' + 10); }
        else
        {
            return Fail(error, JsonLexicalErrorCode::Syntax, position - 1, "Unicode Escapeに16進数以外が含まれています");
        }
        value = (value << 4u) | digit;
    }
    return true;
}
}

bool JsonLexicalReader::DecodeString(std::string_view raw, std::string* output,
    std::size_t& decodedLength, JsonLexicalError& error, bool strictUtf8, std::size_t maxDecodedBytes)
{
    decodedLength = 0;
    if (output != nullptr) { output->clear(); }
    auto Append = [&](const char* bytes, std::size_t count, std::size_t offset)
    {
        if (count > maxDecodedBytes - decodedLength)
        {
            return Fail(error, JsonLexicalErrorCode::Limit, offset, "Stringの展開後サイズが上限を超えています");
        }
        decodedLength += count;
        if (output != nullptr) { output->append(bytes, count); }
        return true;
    };
    std::size_t position = 0;
    while (position < raw.size())
    {
        const std::size_t begin = position;
        const auto c = static_cast<unsigned char>(raw[position++]);
        if (c < 0x20u || c == '"')
        {
            return Fail(error, JsonLexicalErrorCode::Syntax, begin, "String内に未エスケープの制御文字または引用符があります");
        }
        if (c != '\\')
        {
            if (strictUtf8 && c >= 0x80u)
            {
                std::uint32_t code = 0, minimum = 0;
                std::size_t remaining = 0;
                if (c >= 0xC2u && c <= 0xDFu) { code = c & 0x1Fu; minimum = 0x80u; remaining = 1; }
                else if (c >= 0xE0u && c <= 0xEFu) { code = c & 0x0Fu; minimum = 0x800u; remaining = 2; }
                else if (c >= 0xF0u && c <= 0xF4u) { code = c & 0x07u; minimum = 0x10000u; remaining = 3; }
                else { return Fail(error, JsonLexicalErrorCode::Encoding, begin, "不正なUTF-8です"); }
                if (remaining > raw.size() - position)
                {
                    return Fail(error, JsonLexicalErrorCode::Encoding, begin, "UTF-8が途中で終わっています");
                }
                while (remaining-- > 0)
                {
                    const auto byte = static_cast<unsigned char>(raw[position++]);
                    if ((byte & 0xC0u) != 0x80u)
                    {
                        return Fail(error, JsonLexicalErrorCode::Encoding, position - 1, "不正なUTF-8継続Byteです");
                    }
                    code = (code << 6u) | (byte & 0x3Fu);
                }
                if (code < minimum || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
                {
                    return Fail(error, JsonLexicalErrorCode::Encoding, begin, "UTF-8のCode Pointが不正です");
                }
            }
            if (Append(raw.data() + begin, position - begin, begin) == false) { return false; }
            continue;
        }
        if (position == raw.size())
        {
            return Fail(error, JsonLexicalErrorCode::Syntax, position, "String Escapeの途中で入力末尾に到達しました");
        }
        const char escape = raw[position++];
        char decoded = 0;
        switch (escape)
        {
        case '"': decoded = '"'; break;
        case '\\': decoded = '\\'; break;
        case '/': decoded = '/'; break;
        case 'b': decoded = '\b'; break;
        case 'f': decoded = '\f'; break;
        case 'n': decoded = '\n'; break;
        case 'r': decoded = '\r'; break;
        case 't': decoded = '\t'; break;
        case 'u':
        {
            std::uint32_t code = 0;
            if (ReadHex4(raw, position, code, error) == false) { return false; }
            // UTF-16 High Surrogateの場合はLow Surrogateまで読み、Unicode Code Pointへ変換します。
            if (code >= 0xD800u && code <= 0xDBFFu)
            {
                if (raw.size() - position < 6 || raw[position] != '\\' || raw[position + 1] != 'u')
                {
                    return Fail(error, JsonLexicalErrorCode::Syntax, position, "High Surrogateに対応するLow Surrogateがありません");
                }
                position += 2;
                std::uint32_t low = 0;
                if (ReadHex4(raw, position, low, error) == false) { return false; }
                if (low < 0xDC00u || low > 0xDFFFu)
                {
                    return Fail(error, JsonLexicalErrorCode::Syntax, position - 4, "Low Surrogateが不正です");
                }
                code = 0x10000u + ((code - 0xD800u) << 10u) + (low - 0xDC00u);
            }
            else if (code >= 0xDC00u && code <= 0xDFFFu)
            {
                return Fail(error, JsonLexicalErrorCode::Syntax, begin, "Low Surrogateが単独で現れています");
            }
            char bytes[4];
            std::size_t count = 0;
            if (code <= 0x7Fu) { bytes[count++] = static_cast<char>(code); }
            else if (code <= 0x7FFu)
            {
                bytes[count++] = static_cast<char>(0xC0u | (code >> 6u));
                bytes[count++] = static_cast<char>(0x80u | (code & 0x3Fu));
            }
            else if (code <= 0xFFFFu)
            {
                bytes[count++] = static_cast<char>(0xE0u | (code >> 12u));
                bytes[count++] = static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu));
                bytes[count++] = static_cast<char>(0x80u | (code & 0x3Fu));
            }
            else
            {
                bytes[count++] = static_cast<char>(0xF0u | (code >> 18u));
                bytes[count++] = static_cast<char>(0x80u | ((code >> 12u) & 0x3Fu));
                bytes[count++] = static_cast<char>(0x80u | ((code >> 6u) & 0x3Fu));
                bytes[count++] = static_cast<char>(0x80u | (code & 0x3Fu));
            }
            if (Append(bytes, count, begin) == false) { return false; }
            continue;
        }
        default:
            return Fail(error, JsonLexicalErrorCode::Syntax, position - 1, "未対応のString Escapeです");
        }
        if (Append(&decoded, 1, begin) == false) { return false; }
    }
    return true;
}

bool JsonLexicalReader::ReadString(std::string_view text, std::size_t& position, JsonStringInfo& info,
    std::string* output, JsonLexicalError& error, bool strictUtf8,
    std::size_t maxRawBytes, std::size_t maxDecodedBytes)
{
    if (position >= text.size() || text[position] != '"')
    {
        return Fail(error, JsonLexicalErrorCode::Syntax, position, "Stringの開始文字がありません");
    }
    info = {};
    info.Begin = ++position;
    bool escaped = false;
    while (position < text.size())
    {
        const char c = text[position];
        if (escaped == false && c == '"')
        {
            info.Length = position - info.Begin;
            if (DecodeString(text.substr(info.Begin, info.Length), output, info.DecodedLength,
                error, strictUtf8, maxDecodedBytes) == false)
            {
                error.Offset += info.Begin;
                position = error.Offset;
                return false;
            }
            ++position;
            return true;
        }
        if (position - info.Begin >= maxRawBytes)
        {
            return Fail(error, JsonLexicalErrorCode::Limit, position, "StringのRawサイズが上限を超えています");
        }
        if (escaped) { escaped = false; }
        else if (c == '\\') { escaped = true; info.HasEscape = true; }
        ++position;
    }
    return Fail(error, JsonLexicalErrorCode::Syntax, position, "Stringが閉じられていません");
}

bool JsonLexicalReader::ReadNumber(std::string_view text, std::size_t& position,
    JsonNumberInfo& info, JsonLexicalError& error)
{
    info = {position, 0, false};
    if (position < text.size() && text[position] == '-') { ++position; }
    if (position >= text.size())
    {
        return Fail(error, JsonLexicalErrorCode::Syntax, position, "Numberの符号の後に数値がありません");
    }
    if (text[position] == '0') { ++position; }
    else
    {
        if (text[position] < '1' || text[position] > '9')
        {
            return Fail(error, JsonLexicalErrorCode::Syntax, position, "Numberの整数部が不正です");
        }
        while (position < text.size() && IsDigit(text[position])) { ++position; }
    }
    if (position < text.size() && text[position] == '.')
    {
        info.Floating = true;
        ++position;
        if (position == text.size() || IsDigit(text[position]) == false)
        {
            return Fail(error, JsonLexicalErrorCode::Syntax, position, "Numberの小数部が不正です");
        }
        while (position < text.size() && IsDigit(text[position])) { ++position; }
    }
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E'))
    {
        info.Floating = true;
        ++position;
        if (position < text.size() && (text[position] == '+' || text[position] == '-')) { ++position; }
        if (position == text.size() || IsDigit(text[position]) == false)
        {
            return Fail(error, JsonLexicalErrorCode::Syntax, position, "Numberの指数部が不正です");
        }
        while (position < text.size() && IsDigit(text[position])) { ++position; }
    }
    info.Length = position - info.Begin;
    return true;
}
}
}
}
