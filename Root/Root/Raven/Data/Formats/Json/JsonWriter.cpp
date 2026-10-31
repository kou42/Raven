#include "Raven/Data/Formats/Json/JsonWriter.h"
#include "Raven/Data/DataDocument.h"
#include "Raven/Data/DataReader.h"
#include "Raven/Core/JsonLexicalReader.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace Raven
{
namespace Data
{
JsonWriter::JsonWriter(const DataWriteLimits& limits) : m_Limits(limits)
{
}

DataError JsonWriter::GetError() const
{
    return m_Error;
}

bool JsonWriter::Fail(DataErrorCode code)
{
    if (m_Error.Code == DataErrorCode::None)
    {
        m_Error = {code, static_cast<std::uint32_t>(m_OutputBytes - 1)};
    }
    return false;
}

bool JsonWriter::Charge(std::size_t bytes)
{
    if (m_OutputBytes > m_Limits.MaxOutputBytes
        || bytes > m_Limits.MaxOutputBytes - m_OutputBytes)
    {
        return Fail(DataErrorCode::LimitExceeded);
    }
    m_OutputBytes += bytes;
    return true;
}

bool JsonWriter::CheckValue()
{
    if (m_Stack.empty() == true)
    {
        if (m_HasRoot == true) { return Fail(DataErrorCode::InvalidStructure); }
        return true;
    }
    const auto& frame = m_Stack.back();
    if (frame.Object == true && frame.HasKey == false)
    {
        return Fail(DataErrorCode::InvalidStructure);
    }
    if (frame.Count >= m_Limits.MaxContainerElements)
    {
        return Fail(DataErrorCode::LimitExceeded);
    }
    return true;
}

bool JsonWriter::EncodeString(std::string_view value, std::string& output)
{
    if (value.size() > m_Limits.MaxValueBytes) { return Fail(DataErrorCode::LimitExceeded); }
    const char* hex = "0123456789abcdef";
    output = "\"";
    for (unsigned char byte : value)
    {
        switch (byte)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte < 0x20u)
            {
                output += "\\u00";
                output += hex[byte >> 4u];
                output += hex[byte & 15u];
            }
            else
            {
                output += static_cast<char>(byte);
            }
            break;
        }
        // Escapeによる最大6倍の膨張も、確保を続ける前に出力予算で制限します。
        if (m_OutputBytes > m_Limits.MaxOutputBytes
            || output.size() >= m_Limits.MaxOutputBytes - m_OutputBytes)
        {
            return Fail(DataErrorCode::LimitExceeded);
        }
    }
    output += '"';
    std::size_t decodedBytes = 0;
    Core::Detail::JsonLexicalError lexicalError;
    // 制御文字は既にEscape済み。共通字句規則で不正UTF-8も拒否し、独自のUnicode規則を増やしません。
    const std::string_view raw(output.data() + 1, output.size() - 2);
    if (Core::Detail::JsonLexicalReader::DecodeString(raw, nullptr, decodedBytes, lexicalError) == false)
    {
        return Fail(DataErrorCode::InvalidEncoding);
    }
    return true;
}

void JsonWriter::AppendValue(std::string value)
{
    if (m_Stack.empty() == true)
    {
        m_Root = std::move(value);
        m_HasRoot = true;
        return;
    }
    auto& frame = m_Stack.back();
    if (frame.Object == true)
    {
        frame.Members.push_back({std::move(frame.Key), std::move(frame.EncodedKey), std::move(value)});
        frame.HasKey = false;
    }
    else
    {
        if (frame.Count != 0) { frame.Values += ','; }
        frame.Values += value;
    }
    ++frame.Count;
}

bool JsonWriter::Process(Event event, std::string_view value)
{
    if (m_Error.Code != DataErrorCode::None) { return false; }
    if (m_Finished == true) { return Fail(DataErrorCode::InvalidStructure); }
    try
    {
        if (event == Event::Key)
        {
            if (m_Stack.empty() == true || m_Stack.back().Object == false
                || m_Stack.back().HasKey == true)
            {
                return Fail(DataErrorCode::InvalidStructure);
            }
            auto& frame = m_Stack.back();
            if (frame.Count >= m_Limits.MaxContainerElements) { return Fail(DataErrorCode::LimitExceeded); }
            std::string encoded;
            if (EncodeString(value, encoded) == false) { return false; }
            if (Charge(encoded.size()) == false || Charge(frame.Count == 0 ? 1u : 2u) == false)
            {
                return false;
            }
            frame.Key.assign(value);
            frame.EncodedKey = std::move(encoded);
            frame.HasKey = true;
            return true;
        }
        if (event == Event::EndArray || event == Event::EndObject)
        {
            const bool object = event == Event::EndObject;
            if (m_Stack.empty() == true || m_Stack.back().Object != object
                || m_Stack.back().HasKey == true)
            {
                return Fail(DataErrorCode::InvalidStructure);
            }
            auto& frame = m_Stack.back();
            std::string encoded;
            if (object == true)
            {
                std::sort(frame.Members.begin(), frame.Members.end(), [](const Member& a, const Member& b)
                {
                    return a.Key < b.Key;
                });
                encoded = "{";
                for (std::size_t i = 0; i < frame.Members.size(); ++i)
                {
                    const auto& member = frame.Members[i];
                    if (i != 0)
                    {
                        if (member.Key == frame.Members[i - 1].Key) { return Fail(DataErrorCode::DuplicateKey); }
                        encoded += ',';
                    }
                    encoded += member.EncodedKey;
                    encoded += ':';
                    encoded += member.Value;
                }
                encoded += '}';
            }
            else
            {
                encoded = "[";
                encoded += frame.Values;
                encoded += ']';
            }
            m_Stack.pop_back();
            // 開始時にコンテナの括弧と親の区切りを課金済み。閉じるときに二重計上しません。
            AppendValue(std::move(encoded));
            return true;
        }
        if (CheckValue() == false) { return false; }
        const std::size_t separator = m_Stack.empty() == false && m_Stack.back().Object == false
            && m_Stack.back().Count != 0 ? 1u : 0u;
        if (event == Event::BeginArray || event == Event::BeginObject)
        {
            if (m_Stack.size() >= m_Limits.MaxDepth) { return Fail(DataErrorCode::LimitExceeded); }
            if (Charge(2u + separator) == false) { return false; }
            m_Stack.emplace_back();
            m_Stack.back().Object = event == Event::BeginObject;
            return true;
        }
        std::string encoded;
        if (event == Event::String)
        {
            if (EncodeString(value, encoded) == false) { return false; }
        }
        else
        {
            encoded.assign(value);
        }
        if (Charge(encoded.size()) == false || Charge(separator) == false) { return false; }
        AppendValue(std::move(encoded));
        return true;
    }
    catch (const std::bad_alloc&) { return Fail(DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Fail(DataErrorCode::LimitExceeded); }
}

bool JsonWriter::WriteNull() { return Process(Event::Scalar, "null"); }
bool JsonWriter::WriteBool(bool value) { return Process(Event::Scalar, value ? "true" : "false"); }
bool JsonWriter::WriteString(std::string_view value) { return Process(Event::String, value); }
bool JsonWriter::WriteKey(std::string_view key) { return Process(Event::Key, key); }
bool JsonWriter::BeginArray() { return Process(Event::BeginArray); }
bool JsonWriter::EndArray() { return Process(Event::EndArray); }
bool JsonWriter::BeginObject() { return Process(Event::BeginObject); }
bool JsonWriter::EndObject() { return Process(Event::EndObject); }
bool JsonWriter::WriteBinary(std::string_view) { return Fail(DataErrorCode::UnsupportedType); }

bool JsonWriter::WriteInt64(std::int64_t value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return Process(Event::Scalar, {buffer, static_cast<std::size_t>(result.ptr - buffer)});
}

bool JsonWriter::WriteUInt64(std::uint64_t value)
{
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return Process(Event::Scalar, {buffer, static_cast<std::size_t>(result.ptr - buffer)});
}

bool JsonWriter::WriteFloat64(double value)
{
    if (std::isfinite(value) == false) { return Fail(DataErrorCode::NumericRange); }
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 2, value,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) { return Fail(DataErrorCode::NumericRange); }
    std::string_view number(buffer, static_cast<std::size_t>(result.ptr - buffer));
    if (number.find_first_of(".eE") == std::string_view::npos)
    {
        // 1.0 / -0.0を整数Tokenへ変えず、Float64の分類と負のZeroを保持します。
        *result.ptr++ = '.';
        *result.ptr++ = '0';
        number = {buffer, static_cast<std::size_t>(result.ptr - buffer)};
    }
    return Process(Event::Scalar, number);
}

bool JsonWriter::Finish(std::string& output)
{
    if (m_Error.Code != DataErrorCode::None) { return false; }
    if (m_Finished == true || m_HasRoot == false || m_Stack.empty() == false)
    {
        return Fail(DataErrorCode::InvalidStructure);
    }
    if (Charge(0) == false) { return false; }
    try
    {
        m_Root += '\n';
        output.swap(m_Root);
        m_Finished = true;
        return true;
    }
    catch (const std::bad_alloc&) { return Fail(DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Fail(DataErrorCode::LimitExceeded); }
}

bool JsonWriter::Write(DataElement root, std::string& output, DataError* error, const DataWriteLimits& limits)
{
    JsonWriter writer(limits);
    if (DataReader(root).Read(writer, error) == false) { return false; }
    const bool result = writer.Finish(output);
    if (error != nullptr) { *error = writer.GetError(); }
    return result;
}

bool JsonWriter::Write(const DataDocument& document, std::string& output,
    DataError* error, const DataWriteLimits& limits)
{
    return Write(document.GetRoot(), output, error, limits);
}
}
}
