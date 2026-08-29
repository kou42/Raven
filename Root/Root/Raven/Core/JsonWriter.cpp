// Raven/Core/JsonWriter.cpp
#include "Raven/Core/JsonWriter.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace Raven
{
namespace Core
{
namespace
{
bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

void WriteIndent(std::ostringstream& stream, int depth, int indentSize)
{
    stream << std::string(static_cast<std::size_t>(depth * indentSize), ' ');
}

void WriteEscapedString(const std::string& value, std::ostringstream& stream)
{
    stream << '"';
    for (const unsigned char c : value)
    {
        switch (c)
        {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\b': stream << "\\b"; break;
        case '\f': stream << "\\f"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (c < 0x20u)
            {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c) << std::dec << std::setfill(' ');
            }
            else
            {
                stream << static_cast<char>(c);
            }
            break;
        }
    }
    stream << '"';
}

bool WriteValue(
    const JsonValue& value,
    std::ostringstream& stream,
    int depth,
    int indentSize,
    std::string* errorMessage)
{
    const bool pretty = indentSize > 0;
    switch (value.GetType())
    {
    case JsonValue::Type::Null:
        stream << "null";
        return true;
    case JsonValue::Type::Boolean:
        stream << (value.GetBoolean() ? "true" : "false");
        return true;
    case JsonValue::Type::Number:
        if (std::isfinite(value.GetNumber()) == false)
        {
            return SetError(errorMessage, "JSON NumberへNaNまたはInfinityは出力できません");
        }
        stream << std::setprecision(17) << value.GetNumber();
        return true;
    case JsonValue::Type::String:
        WriteEscapedString(value.GetString(), stream);
        return true;
    case JsonValue::Type::Array:
    {
        stream << '[';
        const JsonValue::Array& array = value.GetArray();
        for (std::size_t i = 0; i < array.size(); ++i)
        {
            stream << (i == 0 ? "" : ",");
            if (pretty == true)
            {
                stream << '\n';
                WriteIndent(stream, depth + 1, indentSize);
            }
            if (WriteValue(array[i], stream, depth + 1, indentSize, errorMessage) == false)
            {
                return false;
            }
        }
        if (pretty == true && array.empty() == false)
        {
            stream << '\n';
            WriteIndent(stream, depth, indentSize);
        }
        stream << ']';
        return true;
    }
    case JsonValue::Type::Object:
    {
        stream << '{';
        const JsonValue::Object& object = value.GetObject();
        std::vector<std::string> keys;
        keys.reserve(object.size());
        for (const auto& member : object)
        {
            keys.emplace_back(member.first);
        }
        std::sort(keys.begin(), keys.end());

        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            stream << (i == 0 ? "" : ",");
            if (pretty == true)
            {
                stream << '\n';
                WriteIndent(stream, depth + 1, indentSize);
            }
            WriteEscapedString(keys[i], stream);
            stream << (pretty == true ? ": " : ":");
            if (WriteValue(object.at(keys[i]), stream, depth + 1, indentSize, errorMessage) == false)
            {
                return false;
            }
        }
        if (pretty == true && object.empty() == false)
        {
            stream << '\n';
            WriteIndent(stream, depth, indentSize);
        }
        stream << '}';
        return true;
    }
    }
    return SetError(errorMessage, "未対応のJSON Value種別です");
}
} // namespace

bool JsonWriter::Write(
    const JsonValue& value,
    std::string& outText,
    std::string* errorMessage,
    int indentSize)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    if (indentSize < 0)
    {
        return SetError(errorMessage, "JSONのindentSizeは0以上である必要があります");
    }

    std::ostringstream stream;
    if (WriteValue(value, stream, 0, indentSize, errorMessage) == false)
    {
        return false;
    }
    outText = stream.str();
    outText.push_back('\n');
    return true;
}

} // namespace Core
} // namespace Raven
