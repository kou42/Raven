// Raven/Core/JsonParser.cpp
#include "Raven/Core/JsonParser.h"
#include "Raven/Core/JsonLexicalReader.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace Raven
{
namespace Core
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
        Detail::JsonNumberInfo info;
        Detail::JsonLexicalError error;
        if (Detail::JsonLexicalReader::ReadNumber(m_Text, m_Position, info, error) == false)
        {
            m_Position = error.Offset;
            return Fail(error.Message);
        }
        const std::string numberText(m_Text.substr(info.Begin, info.Length));
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
        Detail::JsonStringInfo info;
        Detail::JsonLexicalError error;
        // 既存Assetの受理規則は維持し、厳密UTF-8検証はData側から明示します。
        if (Detail::JsonLexicalReader::ReadString(m_Text, m_Position, info, &outString, error) == false)
        {
            m_Position = error.Offset;
            return Fail(error.Message);
        }
        return true;
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

} // namespace Core
} // namespace Raven
