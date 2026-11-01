#include "Raven/Data/DataElement.h"
#include "Raven/Data/DataDocument.h"
#include "Raven/Core/JsonLexicalReader.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace Raven
{
namespace Data
{
namespace
{
// 整数→double→整数の往復検査は、丸め後に2^63 / 2^64へ達すると未定義動作を起こします。
// 捨てられる下位bitがすべて0かを整数だけで調べ、精度を失わない場合だけ変換します。
bool IsExactlyRepresentable(std::uint64_t magnitude)
{
    constexpr std::uint64_t MaximumContiguousInteger = std::uint64_t{1} << 53u;
    while (magnitude > MaximumContiguousInteger)
    {
        if ((magnitude & 1u) != 0) { return false; }
        magnitude >>= 1u;
    }
    return true;
}

std::int64_t SignedPayload(const DataToken& token)
{
    std::int64_t value = 0;
    std::memcpy(&value, &token.Payload, sizeof(value));
    return value;
}

double FloatPayload(const DataToken& token)
{
    double value = 0;
    std::memcpy(&value, &token.Payload, sizeof(value));
    return value;
}
}

DataElement::DataElement(const DataDocument* document, TokenId id, TokenId end)
    : m_Document(document), m_Id(id), m_End(end)
{
}

bool DataElement::IsValid() const
{
    return m_Document != nullptr && m_Id < m_End && m_End <= m_Document->m_Tokens.size();
}

const DataToken* DataElement::GetToken() const
{
    return IsValid() ? &m_Document->m_Tokens[m_Id] : nullptr;
}

bool DataElement::IsType(DataType type) const
{
    const DataToken* token = GetToken();
    return token != nullptr && token->Type == type;
}

bool DataElement::TryGetType(DataType& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr) { return false; }
    output = token->Type;
    return true;
}

bool DataElement::TryGetBool(bool& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr || token->Type != DataType::Bool) { return false; }
    output = token->Payload != 0;
    return true;
}

bool DataElement::TryGetInt64(std::int64_t& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr) { return false; }
    if (token->Type == DataType::Int64)
    {
        output = SignedPayload(*token);
        return true;
    }
    if (token->Type == DataType::UInt64)
    {
        if (token->Payload > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
        {
            return false;
        }
        output = static_cast<std::int64_t>(token->Payload);
        return true;
    }
    if (token->Type != DataType::Float64) { return false; }
    const double value = FloatPayload(*token);
    // Int64最大値のdouble化は2^63へ丸められるため、上端を含まない範囲で判定します。
    constexpr double SignedUpperBound = 0x1p63;
    if (std::isfinite(value) == false || value < -SignedUpperBound || value >= SignedUpperBound
        || std::trunc(value) != value)
    {
        return false;
    }
    output = static_cast<std::int64_t>(value);
    return true;
}

bool DataElement::TryGetUInt64(std::uint64_t& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr) { return false; }
    if (token->Type == DataType::UInt64)
    {
        output = token->Payload;
        return true;
    }
    if (token->Type == DataType::Int64)
    {
        const auto value = SignedPayload(*token);
        if (value < 0) { return false; }
        output = static_cast<std::uint64_t>(value);
        return true;
    }
    if (token->Type != DataType::Float64) { return false; }
    const double value = FloatPayload(*token);
    constexpr double UnsignedUpperBound = 0x1p64;
    if (std::isfinite(value) == false || value < 0 || value >= UnsignedUpperBound || std::trunc(value) != value)
    {
        return false;
    }
    // -0.0は整数0として取得できます。符号を必要とする場合はFloat64を取得してください。
    output = static_cast<std::uint64_t>(value);
    return true;
}

bool DataElement::TryGetFloat64(double& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr) { return false; }
    if (token->Type == DataType::Float64)
    {
        output = FloatPayload(*token);
        return true;
    }
    if (token->Type == DataType::UInt64)
    {
        if (IsExactlyRepresentable(token->Payload) == false) { return false; }
        output = static_cast<double>(token->Payload);
        return true;
    }
    if (token->Type != DataType::Int64) { return false; }
    const auto value = SignedPayload(*token);
    // -INT64_MINは表現できないため、+1してから絶対値へ変換します。
    const auto magnitude = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1u
        : static_cast<std::uint64_t>(value);
    if (IsExactlyRepresentable(magnitude) == false) { return false; }
    output = static_cast<double>(value);
    return true;
}

bool DataElement::TryGetRawString(std::string_view& output) const
{
    if (IsType(DataType::String) == false) { return false; }
    return TryGetSource(output);
}

bool DataElement::TryGetStringView(std::string_view& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr || token->Type != DataType::String || token->Payload != DecodedUtf8String)
    {
        return false;
    }
    return TryGetSource(output);
}

bool DataElement::TryGetDecodedString(std::string& output) const
{
    const DataToken* token = GetToken();
    std::string_view raw;
    if (token == nullptr || TryGetRawString(raw) == false) { return false; }
    try
    {
        // 一時Stringで展開を完結し、確保失敗によって利用中の出力を失わないようにします。
        std::string decoded;
        if (token->Payload == JsonEscapedString)
        {
            Core::Detail::JsonLexicalError error;
            std::size_t length = 0;
            if (Core::Detail::JsonLexicalReader::DecodeString(raw, &decoded, length, error) == false)
            {
                return false;
            }
        }
        else if (raw.empty() == false)
        {
            decoded.assign(raw.data(), raw.size());
        }
        output.swap(decoded);
        return true;
    }
    catch (const std::bad_alloc&) { return false; }
    catch (const std::length_error&) { return false; }
}

bool DataElement::TryGetCount(std::uint32_t& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr || (token->Type != DataType::Array && token->Type != DataType::Object))
    {
        return false;
    }
    output = token->Count;
    return true;
}

bool DataElement::TryGetArrayElement(std::uint32_t index, DataElement& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr || token->Type != DataType::Array || index >= token->Count) { return false; }
    auto element = GetFirstChild();
    for (std::uint32_t i = 0; i < index; ++i) { element = element.GetNextSibling(); }
    output = element;
    return true;
}

bool DataElement::TryGetObjectMember(std::uint32_t index, DataElement& key, DataElement& value) const
{
    const DataToken* token = GetToken();
    if (token == nullptr || token->Type != DataType::Object || index >= token->Count || &key == &value)
    {
        return false;
    }
    auto candidate = GetFirstChild();
    for (std::uint32_t i = 0; i < index; ++i)
    {
        candidate = candidate.GetNextSibling().GetNextSibling();
    }
    const auto candidateValue = candidate.GetNextSibling();
    key = candidate;
    value = candidateValue;
    return true;
}

bool DataElement::TryFind(std::string_view key, DataElement& output) const
{
    if (IsType(DataType::Object) == false) { return false; }
    try
    {
        std::string decoded;
        for (auto candidate = GetFirstChild(); candidate.IsValid();)
        {
            std::string_view raw;
            if (candidate.TryGetRawString(raw) == false) { return false; }
            std::string_view text = raw;
            if (candidate.GetToken()->Payload == JsonEscapedString)
            {
                Core::Detail::JsonLexicalError error;
                std::size_t length = 0;
                if (Core::Detail::JsonLexicalReader::DecodeString(raw, &decoded, length, error) == false)
                {
                    return false;
                }
                text = decoded;
            }
            const auto value = candidate.GetNextSibling();
            if (text == key)
            {
                output = value;
                return true;
            }
            candidate = value.GetNextSibling();
        }
        return false;
    }
    catch (const std::bad_alloc&) { return false; }
    catch (const std::length_error&) { return false; }
}

DataElement DataElement::GetFirstChild() const
{
    const DataToken* token = GetToken();
    if (token == nullptr || (token->Type != DataType::Array && token->Type != DataType::Object)
        || token->Count == 0)
    {
        return {};
    }
    return DataElement(m_Document, m_Id + 1u, token->Next);
}

DataElement DataElement::GetNextSibling() const
{
    const DataToken* token = GetToken();
    if (token == nullptr || token->Next >= m_End)
    {
        return {};
    }
    return DataElement(m_Document, token->Next, m_End);
}

bool DataElement::TryGetSource(std::string_view& output) const
{
    const DataToken* token = GetToken();
    if (token == nullptr)
    {
        return false;
    }
    output = std::string_view(m_Document->m_Source).substr(token->Offset, token->Length);
    return true;
}
}
}
