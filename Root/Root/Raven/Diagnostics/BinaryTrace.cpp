#include "Raven/Diagnostics/BinaryTrace.h"

#include <array>
#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace Raven::Diagnostics
{
namespace
{
constexpr std::array<unsigned char, 8> Magic{ 'R', 'V', 'N', 'D', 'I', 'A', 'G', 0 };
constexpr std::size_t FileHeaderBytes = 16;
constexpr std::size_t EventHeaderBytes = 40;
constexpr std::size_t MaxRecordBytes = EventHeaderBytes + MaxDiagnosticTextBytes;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

void Store(unsigned char* bytes, std::uint64_t value, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index)
    {
        bytes[index] = static_cast<unsigned char>(value >> (index * 8));
    }
}

std::uint64_t Load(const unsigned char* bytes, std::size_t count)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index)
    {
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

bool WriteBytes(std::ostream& output, const unsigned char* bytes, std::size_t count)
{
    output.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(count));
    return output.good();
}

TraceReadResult ReadBytes(std::istream& input, unsigned char* bytes, std::size_t count)
{
    try
    {
        input.read(reinterpret_cast<char*>(bytes), static_cast<std::streamsize>(count));
    }
    catch (const std::ios_base::failure&)
    {
        // exceptions()が設定されたstreamでも、通常のEOF/途中切断を結果値へ統一します。
        return input.bad() == true ? TraceReadResult::IOError : TraceReadResult::InvalidData;
    }
    if (input.bad() == true)
    {
        return TraceReadResult::IOError;
    }
    return input.gcount() == static_cast<std::streamsize>(count)
        ? TraceReadResult::Event : TraceReadResult::InvalidData;
}
}

bool WriteBinaryTraceHeader(std::ostream& output)
{
    std::array<unsigned char, FileHeaderBytes> bytes{};
    std::copy(Magic.begin(), Magic.end(), bytes.begin());
    Store(bytes.data() + 8, 1, 2);
    return WriteBytes(output, bytes.data(), bytes.size());
}

bool WriteBinaryTraceEvent(std::ostream& output, const DiagnosticEvent& event)
{
    if (event.Id == 0 || DiagnosticFilter{}.Accepts(event) == false)
    {
        return false;
    }
    std::array<unsigned char, 4 + MaxRecordBytes> bytes{};
    auto* record = bytes.data() + 4;
    Store(record, event.Id, 8);
    Store(record + 8, event.TimestampNanoseconds, 8);
    Store(record + 16, event.ThreadId, 8);
    Store(record + 24, event.Sequence, 8);
    record[32] = static_cast<unsigned char>(event.Category);
    record[33] = static_cast<unsigned char>(event.Severity);
    std::size_t payloadSize = 8;
    if (const auto* integer = std::get_if<std::int64_t>(&event.Payload); integer != nullptr)
    {
        record[34] = 1;
        Store(record + EventHeaderBytes, std::bit_cast<std::uint64_t>(*integer), 8);
    }
    else if (const auto* real = std::get_if<double>(&event.Payload); real != nullptr)
    {
        record[34] = 2;
        Store(record + EventHeaderBytes, std::bit_cast<std::uint64_t>(*real), 8);
    }
    else if (const auto* boolean = std::get_if<bool>(&event.Payload); boolean != nullptr)
    {
        record[34] = 3;
        payloadSize = 1;
        record[EventHeaderBytes] = *boolean == true ? 1 : 0;
    }
    else
    {
        const auto* text = std::get_if<std::string>(&event.Payload);
        if (text == nullptr || text->size() > MaxDiagnosticTextBytes)
        {
            return false;
        }
        record[34] = 4;
        payloadSize = text->size();
        std::copy(text->begin(), text->end(), record + EventHeaderBytes);
    }
    Store(record + 36, payloadSize, 4);
    Store(bytes.data(), EventHeaderBytes + payloadSize, 4);
    return WriteBytes(output, bytes.data(), 4 + EventHeaderBytes + payloadSize);
}

BinaryTraceReader::BinaryTraceReader(std::istream& input)
    : m_Input(input)
{
}

TraceReadResult BinaryTraceReader::ReadNext(DiagnosticEvent& event)
{
    if (m_Result != TraceReadResult::Event)
    {
        return m_Result;
    }
    if (m_HeaderRead == false)
    {
        std::array<unsigned char, FileHeaderBytes> header{};
        m_Result = ReadBytes(m_Input, header.data(), header.size());
        if (m_Result != TraceReadResult::Event)
        {
            return m_Result;
        }
        if (std::equal(Magic.begin(), Magic.end(), header.begin()) == false)
        {
            return m_Result = TraceReadResult::InvalidData;
        }
        if (Load(header.data() + 8, 2) != 1 || Load(header.data() + 10, 2) != 0)
        {
            return m_Result = TraceReadResult::UnsupportedVersion;
        }
        if (Load(header.data() + 12, 4) != 0)
        {
            return m_Result = TraceReadResult::InvalidData;
        }
        m_HeaderRead = true;
    }

    std::array<unsigned char, 4> length{};
    m_Result = ReadBytes(m_Input, length.data(), length.size());
    if (m_Result != TraceReadResult::Event)
    {
        // record境界でのEOFだけを正常終了とし、途中切断はInvalidDataにします。
        if (m_Input.bad() == false && m_Input.eof() == true && m_Input.gcount() == 0)
        {
            m_Result = TraceReadResult::EndOfStream;
        }
        return m_Result;
    }
    const auto size = static_cast<std::size_t>(Load(length.data(), length.size()));
    if (size < EventHeaderBytes || size > MaxRecordBytes)
    {
        return m_Result = TraceReadResult::InvalidData;
    }
    // 外部入力のsizeに応じたHeap確保を行わず、常に上限付きの作業領域で検証します。
    std::array<unsigned char, MaxRecordBytes> record{};
    m_Result = ReadBytes(m_Input, record.data(), size);
    if (m_Result != TraceReadResult::Event)
    {
        return m_Result;
    }
    const auto payloadSize = static_cast<std::size_t>(Load(record.data() + 36, 4));
    if (payloadSize != size - EventHeaderBytes || record[35] != 0)
    {
        return m_Result = TraceReadResult::InvalidData;
    }
    DiagnosticEvent decoded;
    decoded.Id = Load(record.data(), 8);
    decoded.TimestampNanoseconds = Load(record.data() + 8, 8);
    decoded.ThreadId = Load(record.data() + 16, 8);
    decoded.Sequence = Load(record.data() + 24, 8);
    decoded.Category = static_cast<DiagnosticCategory>(record[32]);
    decoded.Severity = static_cast<DiagnosticSeverity>(record[33]);
    if (decoded.Id == 0 || DiagnosticFilter{}.Accepts(decoded) == false)
    {
        return m_Result = TraceReadResult::InvalidData;
    }
    const auto* payload = record.data() + EventHeaderBytes;
    switch (record[34])
    {
    case 1:
    case 2:
        if (payloadSize != 8)
        {
            return m_Result = TraceReadResult::InvalidData;
        }
        if (record[34] == 1)
        {
            decoded.Payload = std::bit_cast<std::int64_t>(Load(payload, 8));
        }
        else
        {
            decoded.Payload = std::bit_cast<double>(Load(payload, 8));
        }
        break;
    case 3:
        if (payloadSize != 1 || payload[0] > 1)
        {
            return m_Result = TraceReadResult::InvalidData;
        }
        decoded.Payload = payload[0] == 1;
        break;
    case 4:
        decoded.Payload = std::string(reinterpret_cast<const char*>(payload), payloadSize);
        break;
    default:
        return m_Result = TraceReadResult::InvalidData;
    }
    // 失敗したrecordでは呼出側のeventを書き換えず、検証済みの所有Payloadだけを公開します。
    event = std::move(decoded);
    return TraceReadResult::Event;
}

BinaryTraceSink::BinaryTraceSink(const std::filesystem::path& path, DiagnosticFilter filter)
    : m_Output(path, std::ios::out | std::ios::binary | std::ios::trunc), m_Filter(filter)
{
    if (WriteBinaryTraceHeader(m_Output) == false)
    {
        m_Output.setstate(std::ios::failbit);
    }
}

bool BinaryTraceSink::IsOpen() const noexcept
{
    return m_Output.is_open() && m_Output.good();
}

bool BinaryTraceSink::Write(const DiagnosticEvent& event)
{
    if (m_Filter.Accepts(event) == true && WriteBinaryTraceEvent(m_Output, event) == false)
    {
        return false;
    }
    return IsOpen();
}

bool BinaryTraceSink::Flush()
{
    m_Output.flush();
    return IsOpen();
}
}
