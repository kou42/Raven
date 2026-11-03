#include "Raven/Data/DataDocument.h"
#include "Raven/Core/JsonValue.h"
#include "Raven/Core/JsonLexicalReader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace Raven
{
namespace Data
{
namespace
{
bool Fail(DataError* error, DataErrorCode code, std::uint32_t offset = 0)
{
    if (error != nullptr)
    {
        *error = {code, offset};
    }
    return false;
}

bool FailLexical(DataError* error, const Core::Detail::JsonLexicalError& lexicalError, std::uint32_t base)
{
    DataErrorCode code = DataErrorCode::InvalidSyntax;
    if (lexicalError.Code == Core::Detail::JsonLexicalErrorCode::Limit)
    {
        code = DataErrorCode::LimitExceeded;
    }
    else if (lexicalError.Code == Core::Detail::JsonLexicalErrorCode::Encoding)
    {
        code = DataErrorCode::InvalidEncoding;
    }
    return Fail(error, code, base + static_cast<std::uint32_t>(lexicalError.Offset));
}

bool CheckLimits(const DataLimits& limits, DataError* error)
{
    // 無効IDの予約と、Token数を表す終端位置の両方を維持します。
    if (limits.MaxTokens == InvalidTokenId || limits.MaxInputBytes == InvalidTokenId)
    {
        return Fail(error, DataErrorCode::InvalidLimits);
    }
    return true;
}

bool IsUtf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80u)
        {
            continue;
        }
        std::uint32_t code = 0;
        std::uint32_t minimum = 0;
        std::size_t count = 0;
        if (lead >= 0xC2u && lead <= 0xDFu)
        {
            code = lead & 0x1Fu; minimum = 0x80u; count = 1;
        }
        else if (lead >= 0xE0u && lead <= 0xEFu)
        {
            code = lead & 0x0Fu; minimum = 0x800u; count = 2;
        }
        else if (lead >= 0xF0u && lead <= 0xF4u)
        {
            code = lead & 0x07u; minimum = 0x10000u; count = 3;
        }
        else
        {
            return false;
        }
        if (count > text.size() - i)
        {
            return false;
        }
        while (count-- > 0)
        {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xC0u) != 0x80u)
            {
                return false;
            }
            code = (code << 6u) | (byte & 0x3Fu);
        }
        if (code < minimum || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
        {
            return false;
        }
    }
    return true;
}

bool Validate(std::string_view source, const std::vector<DataToken>& tokens,
    const DataLimits& limits, DataError* error)
{
    if (source.size() > limits.MaxInputBytes || tokens.size() > limits.MaxTokens)
    {
        return Fail(error, DataErrorCode::LimitExceeded);
    }
    if (tokens.empty() || tokens.front().Next != tokens.size())
    {
        return Fail(error, DataErrorCode::InvalidStructure);
    }
    struct Frame
    {
        TokenId Id;
        TokenId End;
        std::uint64_t Remaining;
        bool Object;
    };
    std::vector<Frame> frames;
    // キーの一時索引は連続領域にまとめ、キーごとのHeap確保を避けます。
    struct Key
    {
        TokenId Parent;
        std::string_view Text;
        std::uint32_t Offset;
        std::size_t DecodedOffset;
        std::size_t DecodedLength;
    };
    std::string decodedKeys;
    std::string scratch;
    std::vector<Key> keys;
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        const DataToken& token = tokens[i];
        while (frames.empty() == false && frames.back().End == i)
        {
            if (frames.back().Remaining != 0)
            {
                return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
            }
            frames.pop_back();
        }
        if (i != 0 && frames.empty())
        {
            return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
        }
        if (token.Offset > source.size() || token.Length > source.size() - token.Offset
            || token.Next <= i || token.Next > tokens.size())
        {
            return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
        }
        if (frames.empty() == false)
        {
            Frame& parent = frames.back();
            if (parent.Remaining == 0 || token.Next > parent.End)
            {
                return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
            }
            if (parent.Object && parent.Remaining % 2u == 0)
            {
                if (token.Type != DataType::String)
                {
                    return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
                }
                Key key{parent.Id, source.substr(token.Offset, token.Length), token.Offset,
                    (std::numeric_limits<std::size_t>::max)(), 0};
                if (token.Length > limits.MaxValueBytes)
                {
                    return Fail(error, DataErrorCode::LimitExceeded, token.Offset);
                }
                if (token.Payload == JsonEscapedString)
                {
                    Core::Detail::JsonLexicalError lexicalError;
                    if (Core::Detail::JsonLexicalReader::DecodeString(key.Text, &scratch,
                        key.DecodedLength, lexicalError, true, limits.MaxValueBytes) == false)
                    {
                        return FailLexical(error, lexicalError, token.Offset);
                    }
                    key.DecodedOffset = decodedKeys.size();
                    if (scratch.size() > limits.MaxInputBytes - decodedKeys.size())
                    {
                        return Fail(error, DataErrorCode::LimitExceeded, token.Offset);
                    }
                    decodedKeys.append(scratch);
                }
                keys.push_back(key);
            }
            --parent.Remaining;
        }
        const bool container = token.Type == DataType::Array || token.Type == DataType::Object;
        if (container)
        {
            if (token.Count > limits.MaxContainerElements || frames.size() >= limits.MaxDepth)
            {
                return Fail(error, DataErrorCode::LimitExceeded, token.Offset);
            }
            // uint64で組数を展開し、Object Count * 2の32 bit overflowを防ぎます。
            frames.push_back({static_cast<TokenId>(i), token.Next,
                static_cast<std::uint64_t>(token.Count) * (token.Type == DataType::Object ? 2u : 1u),
                token.Type == DataType::Object});
        }
        else
        {
            if (token.Next != i + 1u || token.Count != 0)
            {
                return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
            }
            switch (token.Type)
            {
            case DataType::String:
            case DataType::Binary:
                if (token.Length > limits.MaxValueBytes)
                {
                    return Fail(error, DataErrorCode::LimitExceeded, token.Offset);
                }
                if (token.Type == DataType::String)
                {
                    if (token.Payload == JsonEscapedString)
                    {
                        Core::Detail::JsonLexicalError lexicalError;
                        std::size_t decodedLength = 0;
                        if (Core::Detail::JsonLexicalReader::DecodeString(source.substr(token.Offset, token.Length),
                            nullptr, decodedLength, lexicalError, true, limits.MaxValueBytes) == false)
                        {
                            return FailLexical(error, lexicalError, token.Offset);
                        }
                    }
                    else if (token.Payload != DecodedUtf8String)
                    {
                        return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
                    }
                    else if (IsUtf8(source.substr(token.Offset, token.Length)) == false)
                    {
                        return Fail(error, DataErrorCode::InvalidEncoding, token.Offset);
                    }
                }
                break;
            case DataType::Bool:
                if (token.Payload > 1u)
                {
                    return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
                }
                break;
            case DataType::Float64:
            {
                double value = 0;
                static_assert(sizeof(value) == sizeof(token.Payload) && std::numeric_limits<double>::is_iec559,
                    "Float64 requires IEEE 754 64 bit double");
                std::memcpy(&value, &token.Payload, sizeof(value));
                if (std::isfinite(value) == false)
                {
                    return Fail(error, DataErrorCode::NumericRange, token.Offset);
                }
                break;
            }
            case DataType::Null:
            case DataType::Int64:
            case DataType::UInt64:
                break;
            default:
                return Fail(error, DataErrorCode::InvalidStructure, token.Offset);
            }
        }
    }
    for (const Frame& frame : frames)
    {
        if (frame.End != tokens.size() || frame.Remaining != 0)
        {
            return Fail(error, DataErrorCode::InvalidStructure);
        }
    }
    auto KeyText = [&](const Key& key)
    {
        return key.DecodedOffset == (std::numeric_limits<std::size_t>::max)() ? key.Text
            : std::string_view(decodedKeys).substr(key.DecodedOffset, key.DecodedLength);
    };
    std::sort(keys.begin(), keys.end(), [&](const Key& left, const Key& right)
    {
        if (left.Parent != right.Parent) { return left.Parent < right.Parent; }
        if (KeyText(left) != KeyText(right)) { return KeyText(left) < KeyText(right); }
        return left.Offset < right.Offset;
    });
    for (std::size_t i = 1; i < keys.size(); ++i)
    {
        if (keys[i].Parent == keys[i - 1].Parent && KeyText(keys[i]) == KeyText(keys[i - 1]))
        {
            return Fail(error, DataErrorCode::DuplicateKey, keys[i].Offset);
        }
    }
    return true;
}
}

DataDocument::DataDocument(DataDocument&& other) noexcept
    : m_Source(std::move(other.m_Source)), m_Tokens(std::move(other.m_Tokens))
{
    other.m_Source.clear();
    other.m_Tokens.clear();
}

DataDocument& DataDocument::operator=(DataDocument&& other) noexcept
{
    if (this != &other)
    {
        m_Source = std::move(other.m_Source);
        m_Tokens = std::move(other.m_Tokens);
        other.m_Source.clear();
        other.m_Tokens.clear();
    }
    return *this;
}

DataElement DataDocument::GetRoot() const
{
    if (m_Tokens.empty())
    {
        return {};
    }
    return DataElement(this, 0, static_cast<TokenId>(m_Tokens.size()));
}

bool DataDocument::Create(std::string_view source, const std::vector<DataToken>& tokens,
    DataDocument& output, DataError* error, const DataLimits& limits)
{
    if (error != nullptr)
    {
        *error = {};
    }
    if (CheckLimits(limits, error) == false)
    {
        return false;
    }
    try
    {
        if (Validate(source, tokens, limits, error) == false)
        {
            return false;
        }
        DataDocument temporary;
        // 空string_viewのdata()はnullptrでもよいため、空のassignを避けます。
        if (source.empty() == false)
        {
            temporary.m_Source.assign(source.data(), source.size());
        }
        temporary.m_Tokens = tokens;
        output = std::move(temporary);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return Fail(error, DataErrorCode::OutOfMemory);
    }
    catch (const std::length_error&)
    {
        return Fail(error, DataErrorCode::LimitExceeded);
    }
}

bool DataDocument::FromJsonValue(const Core::JsonValue& value, DataDocument& output,
    DataError* error, const DataLimits& limits)
{
    if (error != nullptr)
    {
        *error = {};
    }
    if (CheckLimits(limits, error) == false)
    {
        return false;
    }
    try
    {
        DataDocument temporary;
        struct Work
        {
            const Core::JsonValue* Value;
            const std::string* Key;
            TokenId Finish;
            std::uint32_t Depth;
        };
        std::vector<Work> work{{&value, nullptr, InvalidTokenId, 0}};
        std::uint64_t pendingNodes = 1;
        while (work.empty() == false)
        {
            const Work current = work.back();
            work.pop_back();
            if (current.Finish != InvalidTokenId)
            {
                temporary.m_Tokens[current.Finish].Next = static_cast<TokenId>(temporary.m_Tokens.size());
                continue;
            }
            --pendingNodes;
            if (temporary.m_Tokens.size() >= limits.MaxTokens)
            {
                return Fail(error, DataErrorCode::LimitExceeded);
            }
            const TokenId id = static_cast<TokenId>(temporary.m_Tokens.size());
            DataToken token;
            token.Next = id + 1u;
            const std::string* text = current.Key;
            std::size_t count = 0;
            if (current.Value != nullptr)
            {
                switch (current.Value->GetType())
                {
                case Core::JsonValue::Type::Null: token.Type = DataType::Null; break;
                case Core::JsonValue::Type::Boolean:
                    token.Type = DataType::Bool; token.Payload = current.Value->GetBoolean() ? 1u : 0u;
                    break;
                case Core::JsonValue::Type::Number:
                {
                    token.Type = DataType::Float64;
                    const double number = current.Value->GetNumber();
                    if (std::isfinite(number) == false)
                    {
                        return Fail(error, DataErrorCode::NumericRange);
                    }
                    std::memcpy(&token.Payload, &number, sizeof(number));
                    break;
                }
                case Core::JsonValue::Type::String: text = &current.Value->GetString(); break;
                case Core::JsonValue::Type::Array:
                    token.Type = DataType::Array; count = current.Value->GetArray().size(); break;
                case Core::JsonValue::Type::Object:
                    token.Type = DataType::Object; count = current.Value->GetObject().size(); break;
                default:
                    return Fail(error, DataErrorCode::InvalidStructure);
                }
            }
            if (text != nullptr)
            {
                token.Type = DataType::String;
                if (text->size() > limits.MaxValueBytes
                    || text->size() > limits.MaxInputBytes - temporary.m_Source.size())
                {
                    return Fail(error, DataErrorCode::LimitExceeded);
                }
                token.Offset = static_cast<std::uint32_t>(temporary.m_Source.size());
                token.Length = static_cast<std::uint32_t>(text->size());
                if (IsUtf8(*text) == false)
                {
                    return Fail(error, DataErrorCode::InvalidEncoding, token.Offset);
                }
                temporary.m_Source.append(*text);
            }
            const bool container = token.Type == DataType::Array || token.Type == DataType::Object;
            if (container)
            {
                if (current.Depth >= limits.MaxDepth || count > limits.MaxContainerElements)
                {
                    return Fail(error, DataErrorCode::LimitExceeded);
                }
                // 子の作業Queueも入力由来。巨大Countによる過剰確保をPush前に拒否します。
                const std::uint64_t children = static_cast<std::uint64_t>(count)
                    * (token.Type == DataType::Object ? 2u : 1u);
                if (children + pendingNodes > limits.MaxTokens - temporary.m_Tokens.size() - 1u)
                {
                    return Fail(error, DataErrorCode::LimitExceeded);
                }
                pendingNodes += children;
                token.Count = static_cast<std::uint32_t>(count);
            }
            temporary.m_Tokens.push_back(token);
            if (container == false)
            {
                continue;
            }
            work.push_back({nullptr, nullptr, id, 0});
            if (token.Type == DataType::Array)
            {
                const auto& array = current.Value->GetArray();
                for (auto it = array.rbegin(); it != array.rend(); ++it)
                {
                    work.push_back({&*it, nullptr, InvalidTokenId, current.Depth + 1u});
                }
            }
            else
            {
                // unordered_mapの反復順へ依存させず、AdapterのTape順を安定させます。
                using Entry = Core::JsonValue::Object::value_type;
                std::vector<const Entry*> entries;
                entries.reserve(count);
                for (const auto& entry : current.Value->GetObject())
                {
                    entries.push_back(&entry);
                }
                std::sort(entries.begin(), entries.end(), [](const Entry* left, const Entry* right)
                {
                    return left->first < right->first;
                });
                for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                {
                    work.push_back({&(*it)->second, nullptr, InvalidTokenId, current.Depth + 1u});
                    work.push_back({nullptr, &(*it)->first, InvalidTokenId, current.Depth + 1u});
                }
            }
        }
        if (Validate(temporary.m_Source, temporary.m_Tokens, limits, error) == false)
        {
            return false;
        }
        output = std::move(temporary);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return Fail(error, DataErrorCode::OutOfMemory);
    }
    catch (const std::length_error&)
    {
        return Fail(error, DataErrorCode::LimitExceeded);
    }
}
}
}
