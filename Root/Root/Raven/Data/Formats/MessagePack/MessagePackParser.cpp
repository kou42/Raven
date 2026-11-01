#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"

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
class Parser
{
public:
    Parser(std::string_view bytes, const DataLimits& limits, DataError* error);
    bool Parse(std::vector<DataToken>& tokens);
private:
    struct Frame { TokenId Id; std::uint64_t Remaining; bool Key; };
    bool Read(unsigned width, std::uint64_t& value);
    bool Value(bool key);
    bool Fail(DataErrorCode code, std::size_t position);
    std::string_view m_Bytes;
    const DataLimits& m_Limits;
    DataError* m_Error;
    std::size_t m_Position = 0;
    std::vector<DataToken>* m_Tokens = nullptr;
    std::vector<Frame> m_Stack;
};

Parser::Parser(std::string_view bytes, const DataLimits& limits, DataError* error)
    : m_Bytes(bytes), m_Limits(limits), m_Error(error)
{
}

bool Parser::Fail(DataErrorCode code, std::size_t position)
{
    if (m_Error != nullptr) { *m_Error = {code, static_cast<std::uint32_t>(position)}; }
    return false;
}

bool Parser::Read(unsigned width, std::uint64_t& value)
{
    if (width > m_Bytes.size() - m_Position)
    {
        return Fail(DataErrorCode::UnexpectedEnd, m_Bytes.size());
    }
    value = 0;
    for (unsigned i = 0; i < width; ++i)
    {
        value = (value << 8u) | static_cast<unsigned char>(m_Bytes[m_Position++]);
    }
    return true;
}

bool Parser::Value(bool key)
{
    const std::size_t start = m_Position;
    std::uint64_t tag = 0;
    if (Read(1, tag) == false) { return false; }
    if (m_Tokens->size() >= m_Limits.MaxTokens) { return Fail(DataErrorCode::LimitExceeded, start); }
    DataToken token;
    token.Offset = static_cast<std::uint32_t>(start);
    token.Next = static_cast<TokenId>(m_Tokens->size() + 1u);
    std::uint64_t length = 0;
    bool container = false;
    unsigned prefix = 0;
    if (tag <= 0x7fu) { token.Type = DataType::UInt64; token.Payload = tag; }
    else if (tag >= 0xe0u)
    {
        token.Type = DataType::Int64;
        token.Payload = UINT64_MAX - (0xffu - tag);
    }
    else if ((tag >= 0xa0u && tag <= 0xbfu) || tag == 0xd9u || tag == 0xdau || tag == 0xdbu)
    {
        token.Type = DataType::String;
        length = tag & 0x1fu;
        if (tag >= 0xd9u) { prefix = 1u << (tag - 0xd9u); }
    }
    else if (tag == 0xc4u || tag == 0xc5u || tag == 0xc6u)
    {
        token.Type = DataType::Binary;
        prefix = 1u << (tag - 0xc4u);
    }
    else if ((tag >= 0x90u && tag <= 0x9fu) || tag == 0xdcu || tag == 0xddu)
    {
        token.Type = DataType::Array; container = true;
        length = tag & 0x0fu;
        if (tag >= 0xdcu) { prefix = tag == 0xdcu ? 2u : 4u; }
    }
    else if ((tag >= 0x80u && tag <= 0x8fu) || tag == 0xdeu || tag == 0xdfu)
    {
        token.Type = DataType::Object; container = true;
        length = tag & 0x0fu;
        if (tag >= 0xdeu) { prefix = tag == 0xdeu ? 2u : 4u; }
    }
    else if (tag == 0xc0u) { token.Type = DataType::Null; }
    else if (tag == 0xc2u || tag == 0xc3u) { token.Type = DataType::Bool; token.Payload = tag - 0xc2u; }
    else if (tag >= 0xccu && tag <= 0xd3u)
    {
        const bool signedValue = tag >= 0xd0u;
        const unsigned width = 1u << (tag - (signedValue ? 0xd0u : 0xccu));
        token.Type = signedValue ? DataType::Int64 : DataType::UInt64;
        if (Read(width, token.Payload) == false) { return false; }
        // 符号拡張はunsignedだけで行い、範囲外のsigned castを避けます。
        if (signedValue == true && width < 8u && (token.Payload & (UINT64_C(1) << (width * 8u - 1u))) != 0)
        {
            token.Payload |= UINT64_MAX << (width * 8u);
        }
    }
    else if (tag == 0xcau || tag == 0xcbu)
    {
        token.Type = DataType::Float64;
        std::uint64_t bits = 0;
        if (Read(tag == 0xcau ? 4u : 8u, bits) == false) { return false; }
        double value = 0;
        if (tag == 0xcau)
        {
            static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE Float32 required");
            const auto narrow = static_cast<std::uint32_t>(bits);
            float small = 0;
            std::memcpy(&small, &narrow, sizeof(small));
            value = small;
        }
        else
        {
            static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "IEEE Float64 required");
            std::memcpy(&value, &bits, sizeof(value));
        }
        if (std::isfinite(value) == false) { return Fail(DataErrorCode::NumericRange, start); }
        std::memcpy(&token.Payload, &value, sizeof(value));
    }
    else
    {
        return Fail(tag == 0xc1u ? DataErrorCode::InvalidSyntax : DataErrorCode::UnsupportedType, start);
    }
    if (key == true && token.Type != DataType::String) { return Fail(DataErrorCode::UnsupportedType, start); }
    if (prefix != 0 && Read(prefix, length) == false) { return false; }
    if (container == true)
    {
        if (length > m_Limits.MaxContainerElements || m_Stack.size() >= m_Limits.MaxDepth)
        {
            return Fail(DataErrorCode::LimitExceeded, start);
        }
        const std::uint64_t children = length * (token.Type == DataType::Object ? 2u : 1u);
        // Countから確保しません。最低1 Byte / Tokenという下限も確保前に検査します。
        if (children > m_Limits.MaxTokens - m_Tokens->size() - 1u)
        {
            return Fail(DataErrorCode::LimitExceeded, start);
        }
        if (children > m_Bytes.size() - m_Position) { return Fail(DataErrorCode::UnexpectedEnd, m_Bytes.size()); }
        token.Count = static_cast<std::uint32_t>(length);
        m_Stack.push_back({static_cast<TokenId>(m_Tokens->size()), children, token.Type == DataType::Object});
    }
    else if (token.Type == DataType::String || token.Type == DataType::Binary)
    {
        if (length > m_Limits.MaxValueBytes) { return Fail(DataErrorCode::LimitExceeded, start); }
        if (length > m_Bytes.size() - m_Position) { return Fail(DataErrorCode::UnexpectedEnd, m_Bytes.size()); }
        token.Offset = static_cast<std::uint32_t>(m_Position);
        token.Length = static_cast<std::uint32_t>(length);
        m_Position += static_cast<std::size_t>(length);
    }
    m_Tokens->push_back(token);
    return true;
}

bool Parser::Parse(std::vector<DataToken>& tokens)
{
    m_Tokens = &tokens;
    if (Value(false) == false) { return false; }
    while (m_Stack.empty() == false)
    {
        auto& frame = m_Stack.back();
        if (frame.Remaining == 0)
        {
            auto& token = tokens[frame.Id];
            token.Next = static_cast<TokenId>(tokens.size());
            token.Length = static_cast<std::uint32_t>(m_Position - token.Offset);
            m_Stack.pop_back();
            continue;
        }
        const bool key = frame.Key;
        if (tokens[frame.Id].Type == DataType::Object) { frame.Key = frame.Key == false; }
        --frame.Remaining;
        if (Value(key) == false) { return false; }
    }
    if (m_Position != m_Bytes.size()) { return Fail(DataErrorCode::TrailingData, m_Position); }
    return true;
}
}

bool MessagePackParser::Parse(std::string_view bytes, DataDocument& output, DataError* error, const DataLimits& limits)
{
    if (error != nullptr) { *error = {}; }
    if (limits.MaxTokens == InvalidTokenId || limits.MaxInputBytes == InvalidTokenId)
    {
        if (error != nullptr) { *error = {DataErrorCode::InvalidLimits, 0}; }
        return false;
    }
    if (bytes.size() > limits.MaxInputBytes)
    {
        if (error != nullptr) { *error = {DataErrorCode::LimitExceeded, 0}; }
        return false;
    }
    try
    {
        std::vector<DataToken> tokens;
        Parser parser(bytes, limits, error);
        if (parser.Parse(tokens) == false) { return false; }
        // UTF-8・重複キーの検査と全成功後のCommitを既存Documentへ集約します。
        return DataDocument::Create(bytes, tokens, output, error, limits);
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
