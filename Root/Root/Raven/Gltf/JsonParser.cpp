// Raven/Gltf/JsonParser.cpp
#include "Raven/Gltf/JsonParser.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace Raven
{
namespace Gltf
{
namespace
{

class ParserImpl
{
public:
    ParserImpl(std::string_view text, std::string* errorMessage)
        : m_Text(text), m_ErrorMessage(errorMessage)
    {
    }

    bool ParseRoot(JsonValue& outValue)
    {
        SkipWhitespace();

        if (ParseValue(outValue) == false)
        {
            return false;
        }

        SkipWhitespace();
        if (IsEnd() == false)
        {
            return Fail("Root JSON Valueの後に余分な文字があります");
        }

        return true;
    }

private:
    bool ParseValue(JsonValue& outValue)
    {
        if (IsEnd())
        {
            return Fail("JSON Valueが必要ですが入力末尾に到達しました");
        }

        const char c = Peek();
        if (c == 'n')
        {
            return ParseNull(outValue);
        }
        if (c == 't' || c == 'f')
        {
            return ParseBoolean(outValue);
        }
        if (c == '"')
        {
            std::string value;
            if (ParseString(value) == false)
            {
                return false;
            }

            outValue = JsonValue(std::move(value));
            return true;
        }
        if (c == '[')
        {
            return ParseArray(outValue);
        }
        if (c == '{')
        {
            return ParseObject(outValue);
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            return ParseNumber(outValue);
        }

        return Fail("JSON Valueとして解釈できない文字です");
    }

    bool ParseNull(JsonValue& outValue)
    {
        if (ConsumeLiteral("null") == false)
        {
            return Fail("nullの構文が不正です");
        }

        outValue = JsonValue();
        return true;
    }

    bool ParseBoolean(JsonValue& outValue)
    {
        if (Peek() == 't')
        {
            if (ConsumeLiteral("true") == false)
            {
                return Fail("trueの構文が不正です");
            }

            outValue = JsonValue(true);
            return true;
        }

        if (ConsumeLiteral("false") == false)
        {
            return Fail("falseの構文が不正です");
        }

        outValue = JsonValue(false);
        return true;
    }

    bool ParseNumber(JsonValue& outValue)
    {
        const std::size_t begin = m_Position;

        if (Peek() == '-')
        {
            Advance();
            if (IsEnd())
            {
                return Fail("Numberの符号の後に数値がありません");
            }
        }

        if (Peek() == '0')
        {
            Advance();
        }
        else
        {
            if (Peek() < '1' || Peek() > '9')
            {
                return Fail("Numberの整数部が不正です");
            }

            while (IsEnd() == false && Peek() >= '0' && Peek() <= '9')
            {
                Advance();
            }
        }

        if (IsEnd() == false && Peek() == '.')
        {
            Advance();
            if (IsEnd() || Peek() < '0' || Peek() > '9')
            {
                return Fail("Numberの小数部が不正です");
            }

            while (IsEnd() == false && Peek() >= '0' && Peek() <= '9')
            {
                Advance();
            }
        }

        if (IsEnd() == false && (Peek() == 'e' || Peek() == 'E'))
        {
            Advance();
            if (IsEnd() == false && (Peek() == '+' || Peek() == '-'))
            {
                Advance();
            }

            if (IsEnd() || Peek() < '0' || Peek() > '9')
            {
                return Fail("Numberの指数部が不正です");
            }

            while (IsEnd() == false && Peek() >= '0' && Peek() <= '9')
            {
                Advance();
            }
        }

        const std::string numberText(m_Text.substr(begin, m_Position - begin));
        char* endPointer = nullptr;
        errno = 0;
        const double value = std::strtod(numberText.c_str(), &endPointer);

        if (endPointer == nullptr || endPointer != numberText.c_str() + numberText.size())
        {
            return Fail("Numberの変換に失敗しました");
        }
        if (errno == ERANGE)
        {
            return Fail("Numberがdoubleの表現範囲を超えています");
        }

        outValue = JsonValue(value);
        return true;
    }

    bool ParseString(std::string& outString)
    {
        if (Consume('"') == false)
        {
            return Fail("Stringの開始文字がありません");
        }

        outString.clear();

        while (IsEnd() == false)
        {
            const unsigned char c = static_cast<unsigned char>(Peek());
            Advance();

            if (c == '"')
            {
                return true;
            }

            if (c < 0x20u)
            {
                return Fail("String内に未エスケープの制御文字があります");
            }

            if (c != '\\')
            {
                outString.push_back(static_cast<char>(c));
                continue;
            }

            if (IsEnd())
            {
                return Fail("String Escapeの途中で入力末尾に到達しました");
            }

            const char escape = Peek();
            Advance();

            switch (escape)
            {
            case '"': outString.push_back('"'); break;
            case '\\': outString.push_back('\\'); break;
            case '/': outString.push_back('/'); break;
            case 'b': outString.push_back('\b'); break;
            case 'f': outString.push_back('\f'); break;
            case 'n': outString.push_back('\n'); break;
            case 'r': outString.push_back('\r'); break;
            case 't': outString.push_back('\t'); break;
            case 'u':
            {
                std::uint32_t codePoint = 0;
                if (ParseUnicodeEscape(codePoint) == false)
                {
                    return false;
                }

                AppendUtf8(codePoint, outString);
                break;
            }
            default:
                return Fail("未対応のString Escapeです");
            }
        }

        return Fail("Stringが閉じられていません");
    }

    bool ParseUnicodeEscape(std::uint32_t& outCodePoint)
    {
        std::uint32_t first = 0;
        if (ParseHex4(first) == false)
        {
            return false;
        }

        // UTF-16 High Surrogateの場合はLow Surrogateまで読み、Unicode Code Pointへ変換します。
        if (first >= 0xD800u && first <= 0xDBFFu)
        {
            if (Remaining() < 6 || m_Text[m_Position] != '\\' || m_Text[m_Position + 1] != 'u')
            {
                return Fail("High Surrogateに対応するLow Surrogateがありません");
            }

            m_Position += 2;

            std::uint32_t second = 0;
            if (ParseHex4(second) == false)
            {
                return false;
            }
            if (second < 0xDC00u || second > 0xDFFFu)
            {
                return Fail("Low Surrogateが不正です");
            }

            outCodePoint = 0x10000u + ((first - 0xD800u) << 10u) + (second - 0xDC00u);
            return true;
        }

        if (first >= 0xDC00u && first <= 0xDFFFu)
        {
            return Fail("Low Surrogateが単独で現れています");
        }

        outCodePoint = first;
        return true;
    }

    bool ParseHex4(std::uint32_t& outValue)
    {
        if (Remaining() < 4)
        {
            return Fail("Unicode Escapeが4桁未満です");
        }

        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = Peek();
            Advance();

            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9')
            {
                digit = static_cast<std::uint32_t>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            }
            else
            {
                return Fail("Unicode Escapeに16進数以外が含まれています");
            }

            value = (value << 4u) | digit;
        }

        outValue = value;
        return true;
    }

    static void AppendUtf8(std::uint32_t codePoint, std::string& output)
    {
        if (codePoint <= 0x7Fu)
        {
            output.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FFu)
        {
            output.push_back(static_cast<char>(0xC0u | (codePoint >> 6u)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        }
        else if (codePoint <= 0xFFFFu)
        {
            output.push_back(static_cast<char>(0xE0u | (codePoint >> 12u)));
            output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0u | (codePoint >> 18u)));
            output.push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        }
    }

    bool ParseArray(JsonValue& outValue)
    {
        if (Consume('[') == false)
        {
            return Fail("Arrayの開始文字がありません");
        }

        JsonValue::Array values;
        SkipWhitespace();

        if (Consume(']'))
        {
            outValue = JsonValue(std::move(values));
            return true;
        }

        while (true)
        {
            JsonValue value;
            if (ParseValue(value) == false)
            {
                return false;
            }
            values.emplace_back(std::move(value));

            SkipWhitespace();
            if (Consume(']'))
            {
                outValue = JsonValue(std::move(values));
                return true;
            }
            if (Consume(',') == false)
            {
                return Fail("Array要素の後に','または']'が必要です");
            }

            SkipWhitespace();
        }
    }

    bool ParseObject(JsonValue& outValue)
    {
        if (Consume('{') == false)
        {
            return Fail("Objectの開始文字がありません");
        }

        JsonValue::Object values;
        SkipWhitespace();

        if (Consume('}'))
        {
            outValue = JsonValue(std::move(values));
            return true;
        }

        while (true)
        {
            if (IsEnd() || Peek() != '"')
            {
                return Fail("Object KeyはStringである必要があります");
            }

            std::string key;
            if (ParseString(key) == false)
            {
                return false;
            }

            SkipWhitespace();
            if (Consume(':') == false)
            {
                return Fail("Object Keyの後に':'が必要です");
            }

            SkipWhitespace();
            JsonValue value;
            if (ParseValue(value) == false)
            {
                return false;
            }

            // JSON Object内の同一Key重複は後勝ちにせず、入力データの曖昧さを拒否します。
            const auto inserted = values.emplace(std::move(key), std::move(value));
            if (inserted.second == false)
            {
                return Fail("Object内に重複Keyがあります");
            }

            SkipWhitespace();
            if (Consume('}'))
            {
                outValue = JsonValue(std::move(values));
                return true;
            }
            if (Consume(',') == false)
            {
                return Fail("Object Memberの後に','または'}'が必要です");
            }

            SkipWhitespace();
        }
    }

    void SkipWhitespace()
    {
        while (IsEnd() == false)
        {
            const char c = Peek();
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            {
                break;
            }

            Advance();
        }
    }

    bool Consume(char expected)
    {
        if (IsEnd() || Peek() != expected)
        {
            return false;
        }

        Advance();
        return true;
    }

    bool ConsumeLiteral(std::string_view literal)
    {
        if (Remaining() < literal.size())
        {
            return false;
        }
        if (m_Text.substr(m_Position, literal.size()) != literal)
        {
            return false;
        }

        m_Position += literal.size();
        return true;
    }

    bool Fail(const char* message)
    {
        if (m_ErrorMessage != nullptr)
        {
            *m_ErrorMessage = std::string(message)
                + " (byte " + std::to_string(m_Position) + ")";
        }

        return false;
    }

    bool IsEnd() const
    {
        return m_Position >= m_Text.size();
    }

    char Peek() const
    {
        return m_Text[m_Position];
    }

    void Advance()
    {
        ++m_Position;
    }

    std::size_t Remaining() const
    {
        return m_Text.size() - m_Position;
    }

private:
    std::string_view m_Text;
    std::size_t m_Position = 0;
    std::string* m_ErrorMessage = nullptr;
};

} // namespace

bool JsonParser::Parse(
    std::string_view text,
    JsonValue& outValue,
    std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }

    ParserImpl parser(text, errorMessage);
    return parser.ParseRoot(outValue);
}

} // namespace Gltf
} // namespace Raven
