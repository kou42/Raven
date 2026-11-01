#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include "Raven/Data/DataDocument.h"
#include "Raven/Data/DataReader.h"
#include "Raven/Data/DataUtf8.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace Raven
{
namespace Data
{
namespace
{
// MessagePackはnetwork byte order。host endianやC++ struct配置に依存しません。
void Put(std::string& output, unsigned tag, std::uint64_t value, unsigned width)
{
    output += static_cast<char>(tag);
    for (unsigned i = width; i > 0; --i)
    {
        output += static_cast<char>((value >> ((i - 1u) * 8u)) & 0xffu);
    }
}

void ContainerHeader(std::string& output, bool object, std::uint32_t count)
{
    if (count < 16u) { output += static_cast<char>((object ? 0x80u : 0x90u) | count); }
    else { Put(output, object ? (count <= 65535u ? 0xdeu : 0xdfu)
        : (count <= 65535u ? 0xdcu : 0xddu), count, count <= 65535u ? 2u : 4u); }
}

}

MessagePackWriter::MessagePackWriter(const DataWriteLimits& limits) : m_Limits(limits)
{
}

DataError MessagePackWriter::GetError() const
{
    return m_Error;
}

bool MessagePackWriter::Fail(DataErrorCode code)
{
    if (m_Error.Code == DataErrorCode::None)
    {
        m_Error = {code, static_cast<std::uint32_t>(m_OutputBytes)};
    }
    return false;
}

bool MessagePackWriter::Charge(std::size_t bytes)
{
    if (m_OutputBytes > m_Limits.MaxOutputBytes
        || bytes > m_Limits.MaxOutputBytes - m_OutputBytes)
    {
        return Fail(DataErrorCode::LimitExceeded);
    }
    m_OutputBytes += bytes;
    return true;
}

bool MessagePackWriter::CheckValue()
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

bool MessagePackWriter::EncodeString(std::string_view value, std::string& output)
{
    if (value.size() > m_Limits.MaxValueBytes) { return Fail(DataErrorCode::LimitExceeded); }
    if (Detail::IsUtf8(value) == false) { return Fail(DataErrorCode::InvalidEncoding); }
    if (value.size() < 32u) { output += static_cast<char>(0xa0u | value.size()); }
    else { Put(output, value.size() <= 255u ? 0xd9u : (value.size() <= 65535u ? 0xdau : 0xdbu),
        value.size(), value.size() <= 255u ? 1u : (value.size() <= 65535u ? 2u : 4u)); }
    if (Charge(output.size()) == false || Charge(value.size()) == false) { return false; }
    output.append(value);
    return true;
}

void MessagePackWriter::AppendValue(std::string value)
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
        frame.Values += value;
    }
    ++frame.Count;
}

bool MessagePackWriter::Process(Event event, std::string_view value)
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
            const std::size_t extra = frame.Count < 16u ? 0u : (frame.Count <= 65535u ? 2u : 4u);
            if (Charge(extra) == false) { return false; }
            std::string encoded;
            if (object == true)
            {
                std::sort(frame.Members.begin(), frame.Members.end(), [](const Member& a, const Member& b)
                {
                    return a.Key < b.Key;
                });
                ContainerHeader(encoded, true, frame.Count);
                for (std::size_t i = 0; i < frame.Members.size(); ++i)
                {
                    const auto& member = frame.Members[i];
                    if (i != 0)
                    {
                        if (member.Key == frame.Members[i - 1].Key) { return Fail(DataErrorCode::DuplicateKey); }
                    }
                    encoded += member.EncodedKey;
                    encoded += member.Value;
                }
            }
            else
            {
                ContainerHeader(encoded, false, frame.Count);
                encoded += frame.Values;
            }
            m_Stack.pop_back();
            // 子のPayloadは既に課金済み。EndではHeaderの増分だけを課金します。
            AppendValue(std::move(encoded));
            return true;
        }
        if (CheckValue() == false) { return false; }
        if (event == Event::BeginArray || event == Event::BeginObject)
        {
            if (m_Stack.size() >= m_Limits.MaxDepth) { return Fail(DataErrorCode::LimitExceeded); }
            if (Charge(1u) == false) { return false; }
            m_Stack.emplace_back();
            m_Stack.back().Object = event == Event::BeginObject;
            return true;
        }
        std::string encoded;
        if (event == Event::String)
        {
            if (EncodeString(value, encoded) == false) { return false; }
        }
        else if (event == Event::Binary)
        {
            if (value.size() > m_Limits.MaxValueBytes) { return Fail(DataErrorCode::LimitExceeded); }
            Put(encoded, value.size() <= 255u ? 0xc4u : (value.size() <= 65535u ? 0xc5u : 0xc6u),
                value.size(), value.size() <= 255u ? 1u : (value.size() <= 65535u ? 2u : 4u));
            if (Charge(encoded.size()) == false || Charge(value.size()) == false) { return false; }
            encoded.append(value);
        }
        else
        {
            if (Charge(value.size()) == false) { return false; }
            encoded.assign(value);
        }
        AppendValue(std::move(encoded));
        return true;
    }
    catch (const std::bad_alloc&) { return Fail(DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Fail(DataErrorCode::LimitExceeded); }
}

bool MessagePackWriter::WriteNull()
{
    const char byte = static_cast<char>(0xc0);
    return Process(Event::Scalar, {&byte, 1});
}
bool MessagePackWriter::WriteBool(bool value)
{
    const char byte = static_cast<char>(value ? 0xc3 : 0xc2);
    return Process(Event::Scalar, {&byte, 1});
}
bool MessagePackWriter::WriteString(std::string_view value) { return Process(Event::String, value); }
bool MessagePackWriter::WriteKey(std::string_view key) { return Process(Event::Key, key); }
bool MessagePackWriter::BeginArray() { return Process(Event::BeginArray); }
bool MessagePackWriter::EndArray() { return Process(Event::EndArray); }
bool MessagePackWriter::BeginObject() { return Process(Event::BeginObject); }
bool MessagePackWriter::EndObject() { return Process(Event::EndObject); }

bool MessagePackWriter::WriteBinary(std::string_view value)
{
    // 大きなPayloadはProcessのtry内で確保し、例外を外へ漏らしません。
    return Process(Event::Binary, value);
}

bool MessagePackWriter::WriteInt64(std::int64_t value)
{
    if (value >= 0) { return WriteUInt64(static_cast<std::uint64_t>(value)); }
    char bytes[9];
    const unsigned width = value >= -32 ? 0u : (value >= -128 ? 1u : (value >= -32768 ? 2u
        : (value >= INT32_MIN ? 4u : 8u)));
    bytes[0] = static_cast<char>(width == 0 ? static_cast<unsigned char>(value)
        : (width == 1 ? 0xd0u : (width == 2 ? 0xd1u : (width == 4 ? 0xd2u : 0xd3u))));
    const auto bits = static_cast<std::uint64_t>(value);
    for (unsigned i = 0; i < width; ++i) { bytes[i + 1u] = static_cast<char>((bits >> ((width - i - 1u) * 8u)) & 0xffu); }
    return Process(Event::Scalar, {bytes, width + 1u});
}

bool MessagePackWriter::WriteUInt64(std::uint64_t value)
{
    char bytes[9];
    const unsigned width = value <= 127u ? 0u : (value <= 255u ? 1u : (value <= 65535u ? 2u
        : (value <= UINT32_MAX ? 4u : 8u)));
    bytes[0] = static_cast<char>(width == 0 ? value
        : (width == 1 ? 0xccu : (width == 2 ? 0xcdu : (width == 4 ? 0xceu : 0xcfu))));
    for (unsigned i = 0; i < width; ++i) { bytes[i + 1u] = static_cast<char>((value >> ((width - i - 1u) * 8u)) & 0xffu); }
    return Process(Event::Scalar, {bytes, width + 1u});
}

bool MessagePackWriter::WriteFloat64(double value)
{
    if (std::isfinite(value) == false) { return Fail(DataErrorCode::NumericRange); }
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "IEEE Float64 required");
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char bytes[9];
    bytes[0] = static_cast<char>(0xcb);
    for (unsigned i = 0; i < 8; ++i) { bytes[i + 1] = static_cast<char>((bits >> ((7u - i) * 8u)) & 0xffu); }
    return Process(Event::Scalar, {bytes, sizeof(bytes)});
}

bool MessagePackWriter::Finish(std::string& output)
{
    if (m_Error.Code != DataErrorCode::None) { return false; }
    if (m_Finished == true || m_HasRoot == false || m_Stack.empty() == false)
    {
        return Fail(DataErrorCode::InvalidStructure);
    }
    if (Charge(0) == false) { return false; }
    try
    {
        output.swap(m_Root);
        m_Finished = true;
        return true;
    }
    catch (const std::bad_alloc&) { return Fail(DataErrorCode::OutOfMemory); }
    catch (const std::length_error&) { return Fail(DataErrorCode::LimitExceeded); }
}

bool MessagePackWriter::Write(DataElement root, std::string& output, DataError* error, const DataWriteLimits& limits)
{
    MessagePackWriter writer(limits);
    if (DataReader(root).Read(writer, error) == false) { return false; }
    const bool result = writer.Finish(output);
    if (error != nullptr) { *error = writer.GetError(); }
    return result;
}

bool MessagePackWriter::Write(const DataDocument& document, std::string& output,
    DataError* error, const DataWriteLimits& limits)
{
    return Write(document.GetRoot(), output, error, limits);
}
}
}
