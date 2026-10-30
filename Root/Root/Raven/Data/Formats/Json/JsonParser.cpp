#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Core/JsonLexicalReader.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>

namespace Raven
{
namespace Data
{
namespace
{
using Core::Detail::JsonLexicalReader;

class ParserImpl
{
public:
    ParserImpl(std::string_view text, const DataLimits& limits, DataError* error);
    bool Parse(std::vector<DataToken>& tokens);

private:
    enum class State { ArrayFirst, ArrayValue, ArrayComma, ObjectFirst, ObjectKey, ObjectColon, ObjectValue, ObjectComma };
    struct Frame { TokenId Id; State Next; };
    bool Value();
    bool String();
    bool Number();
    bool Literal(std::string_view literal, DataType type, std::uint64_t payload);
    bool Key();
    void Close();
    void Whitespace();
    bool Fail(DataErrorCode code, std::size_t offset);
    bool LexicalFailure(const Core::Detail::JsonLexicalError& error);
    std::string_view m_Text;
    const DataLimits& m_Limits;
    DataError* m_Error;
    std::size_t m_Position = 0;
    std::vector<DataToken>* m_Tokens = nullptr;
    std::vector<Frame> m_Frames;
};

ParserImpl::ParserImpl(std::string_view text, const DataLimits& limits, DataError* error)
    : m_Text(text), m_Limits(limits), m_Error(error)
{
}

bool ParserImpl::Fail(DataErrorCode code, std::size_t offset)
{
    if (code == DataErrorCode::InvalidSyntax && offset == m_Text.size())
    {
        code = DataErrorCode::UnexpectedEnd;
    }
    if (m_Error != nullptr)
    {
        *m_Error = {code, static_cast<std::uint32_t>(offset)};
    }
    return false;
}

bool ParserImpl::LexicalFailure(const Core::Detail::JsonLexicalError& error)
{
    DataErrorCode code = DataErrorCode::InvalidSyntax;
    if (error.Code == Core::Detail::JsonLexicalErrorCode::Encoding) { code = DataErrorCode::InvalidEncoding; }
    else if (error.Code == Core::Detail::JsonLexicalErrorCode::Limit) { code = DataErrorCode::LimitExceeded; }
    return Fail(code, error.Offset);
}

void ParserImpl::Whitespace()
{
    while (m_Position < m_Text.size())
    {
        const char c = m_Text[m_Position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') { break; }
        ++m_Position;
    }
}

bool ParserImpl::String()
{
    Core::Detail::JsonStringInfo info;
    Core::Detail::JsonLexicalError error;
    if (JsonLexicalReader::ReadString(m_Text, m_Position, info, nullptr, error, true,
        m_Limits.MaxValueBytes, m_Limits.MaxValueBytes) == false)
    {
        return LexicalFailure(error);
    }
    DataToken token;
    token.Type = DataType::String;
    token.Offset = static_cast<std::uint32_t>(info.Begin);
    token.Length = static_cast<std::uint32_t>(info.Length);
    token.Next = static_cast<TokenId>(m_Tokens->size() + 1u);
    token.Payload = info.HasEscape ? JsonEscapedString : DecodedUtf8String;
    m_Tokens->push_back(token);
    return true;
}

bool ParserImpl::Number()
{
    Core::Detail::JsonNumberInfo info;
    Core::Detail::JsonLexicalError error;
    if (JsonLexicalReader::ReadNumber(m_Text, m_Position, info, error) == false)
    {
        return LexicalFailure(error);
    }
    DataToken token;
    token.Offset = static_cast<std::uint32_t>(info.Begin);
    token.Length = static_cast<std::uint32_t>(info.Length);
    token.Next = static_cast<TokenId>(m_Tokens->size() + 1u);
    const char* begin = m_Text.data() + info.Begin;
    const char* end = begin + info.Length;
    std::from_chars_result converted;
    if (info.Floating)
    {
        token.Type = DataType::Float64;
        double value = 0;
        converted = std::from_chars(begin, end, value, std::chars_format::general);
        // 変換はlocale非依存。表現可能なsubnormalは保持し、0へのUnderflowは拒否します。
        if (converted.ec != std::errc{} || std::isfinite(value) == false)
        {
            return Fail(DataErrorCode::NumericRange, info.Begin);
        }
        std::memcpy(&token.Payload, &value, sizeof(value));
    }
    else if (*begin == '-')
    {
        token.Type = DataType::Int64;
        std::int64_t value = 0;
        converted = std::from_chars(begin, end, value);
        std::memcpy(&token.Payload, &value, sizeof(value));
    }
    else
    {
        token.Type = DataType::UInt64;
        converted = std::from_chars(begin, end, token.Payload);
    }
    if (converted.ec != std::errc{} || converted.ptr != end)
    {
        return Fail(DataErrorCode::NumericRange, info.Begin);
    }
    m_Tokens->push_back(token);
    return true;
}

bool ParserImpl::Literal(std::string_view literal, DataType type, std::uint64_t payload)
{
    for (char c : literal)
    {
        if (m_Position == m_Text.size() || m_Text[m_Position] != c)
        {
            return Fail(DataErrorCode::InvalidSyntax, m_Position);
        }
        ++m_Position;
    }
    DataToken token;
    token.Type = type;
    token.Offset = static_cast<std::uint32_t>(m_Position - literal.size());
    token.Length = static_cast<std::uint32_t>(literal.size());
    token.Next = static_cast<TokenId>(m_Tokens->size() + 1u);
    token.Payload = payload;
    m_Tokens->push_back(token);
    return true;
}

bool ParserImpl::Value()
{
    Whitespace();
    if (m_Position == m_Text.size()) { return Fail(DataErrorCode::UnexpectedEnd, m_Position); }
    if (m_Tokens->size() >= m_Limits.MaxTokens) { return Fail(DataErrorCode::LimitExceeded, m_Position); }
    const char c = m_Text[m_Position];
    if (c == '"') { return String(); }
    if (c == 'n') { return Literal("null", DataType::Null, 0); }
    if (c == 't') { return Literal("true", DataType::Bool, 1); }
    if (c == 'f') { return Literal("false", DataType::Bool, 0); }
    if (c == '-' || (c >= '0' && c <= '9')) { return Number(); }
    if (c != '[' && c != '{') { return Fail(DataErrorCode::InvalidSyntax, m_Position); }
    // 空コンテナもOpen深さへ含め、Stack確保より先に上限を検査します。
    if (m_Frames.size() >= m_Limits.MaxDepth) { return Fail(DataErrorCode::LimitExceeded, m_Position); }
    const TokenId id = static_cast<TokenId>(m_Tokens->size());
    DataToken token;
    token.Type = c == '[' ? DataType::Array : DataType::Object;
    token.Offset = static_cast<std::uint32_t>(m_Position++);
    m_Tokens->push_back(token);
    m_Frames.push_back({id, c == '[' ? State::ArrayFirst : State::ObjectFirst});
    return true;
}

bool ParserImpl::Key()
{
    if (m_Tokens->size() >= m_Limits.MaxTokens) { return Fail(DataErrorCode::LimitExceeded, m_Position); }
    if (m_Position == m_Text.size() || m_Text[m_Position] != '"')
    {
        return Fail(DataErrorCode::InvalidSyntax, m_Position);
    }
    const TokenId parent = m_Frames.back().Id;
    if ((*m_Tokens)[parent].Count >= m_Limits.MaxContainerElements)
    {
        return Fail(DataErrorCode::LimitExceeded, m_Position);
    }
    ++(*m_Tokens)[parent].Count;
    if (String() == false) { return false; }
    m_Frames.back().Next = State::ObjectColon;
    return true;
}

void ParserImpl::Close()
{
    DataToken& token = (*m_Tokens)[m_Frames.back().Id];
    token.Length = static_cast<std::uint32_t>(++m_Position - token.Offset);
    token.Next = static_cast<TokenId>(m_Tokens->size());
    m_Frames.pop_back();
}

bool ParserImpl::Parse(std::vector<DataToken>& tokens)
{
    m_Tokens = &tokens;
    if (Value() == false) { return false; }
    while (m_Frames.empty() == false)
    {
        Whitespace();
        if (m_Position == m_Text.size()) { return Fail(DataErrorCode::UnexpectedEnd, m_Position); }
        const char c = m_Text[m_Position];
        const State state = m_Frames.back().Next;
        const TokenId parent = m_Frames.back().Id;
        switch (state)
        {
        case State::ArrayFirst:
            if (c == ']') { Close(); break; }
            [[fallthrough]];
        case State::ArrayValue:
            if ((*m_Tokens)[parent].Count >= m_Limits.MaxContainerElements)
            {
                return Fail(DataErrorCode::LimitExceeded, m_Position);
            }
            ++(*m_Tokens)[parent].Count;
            m_Frames.back().Next = State::ArrayComma;
            if (Value() == false) { return false; }
            break;
        case State::ArrayComma:
            if (c == ']') { Close(); }
            else if (c == ',') { ++m_Position; m_Frames.back().Next = State::ArrayValue; }
            else { return Fail(DataErrorCode::InvalidSyntax, m_Position); }
            break;
        case State::ObjectFirst:
            if (c == '}') { Close(); break; }
            [[fallthrough]];
        case State::ObjectKey:
            if (Key() == false) { return false; }
            break;
        case State::ObjectColon:
            if (c != ':') { return Fail(DataErrorCode::InvalidSyntax, m_Position); }
            ++m_Position;
            m_Frames.back().Next = State::ObjectValue;
            break;
        case State::ObjectValue:
            m_Frames.back().Next = State::ObjectComma;
            if (Value() == false) { return false; }
            break;
        case State::ObjectComma:
            if (c == '}') { Close(); }
            else if (c == ',') { ++m_Position; m_Frames.back().Next = State::ObjectKey; }
            else { return Fail(DataErrorCode::InvalidSyntax, m_Position); }
            break;
        }
    }
    Whitespace();
    if (m_Position != m_Text.size()) { return Fail(DataErrorCode::TrailingData, m_Position); }
    return true;
}
}

bool JsonParser::Parse(std::string_view text, DataDocument& output, DataError* error, const DataLimits& limits)
{
    if (error != nullptr) { *error = {}; }
    if (limits.MaxTokens == InvalidTokenId || limits.MaxInputBytes == InvalidTokenId)
    {
        if (error != nullptr) { *error = {DataErrorCode::InvalidLimits, 0}; }
        return false;
    }
    if (text.size() > limits.MaxInputBytes)
    {
        if (error != nullptr) { *error = {DataErrorCode::LimitExceeded, 0}; }
        return false;
    }
    try
    {
        std::vector<DataToken> tokens;
        ParserImpl parser(text, limits, error);
        if (parser.Parse(tokens) == false) { return false; }
        // Createも一時Documentで完結するため、textがoutputのSourceを参照しても安全です。
        // 重複キー検査はDecodedキーを使うDocument検証へ集約します。
        return DataDocument::Create(text, tokens, output, error, limits);
    }
    catch (const std::bad_alloc&)
    {
        if (error != nullptr) { *error = {DataErrorCode::OutOfMemory, 0}; }
        return false;
    }
    catch (const std::length_error&)
    {
        if (error != nullptr) { *error = {DataErrorCode::LimitExceeded, 0}; }
        return false;
    }
}
}
}
