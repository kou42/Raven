#include "Raven/Diagnostics/RemoteProtocol.h"
#include <algorithm>
#include <sstream>

namespace Raven::Diagnostics
{
namespace
{
void Store(char* target, std::uint64_t value, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index) { target[index] = static_cast<char>(value >> (index * 8)); }
}
std::uint64_t Load(const char* source, std::size_t count)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index) { value |= static_cast<std::uint64_t>(static_cast<unsigned char>(source[index])) << (index * 8); }
    return value;
}
}
std::string EncodeRemoteEvent(const DiagnosticEvent& event)
{
    std::ostringstream output(std::ios::binary);
    if (WriteBinaryTraceHeader(output) == false || WriteBinaryTraceEvent(output, event) == false) { return {}; }
    return output.str();
}
bool DecodeRemoteEvent(const RemotePacket& packet, DiagnosticEvent& event)
{
    std::istringstream input(packet.EventBytes, std::ios::binary);
    BinaryTraceReader reader(input); DiagnosticEvent decoded, trailing;
    if (reader.ReadNext(decoded) != TraceReadResult::Event || reader.ReadNext(trailing) != TraceReadResult::EndOfStream) { return false; }
    event = std::move(decoded); return true;
}
std::string EncodeRemotePacket(const RemotePacket& packet)
{
    const auto kind = static_cast<unsigned>(packet.Kind);
    if (kind < 1 || kind > 4 || (packet.SessionHigh == 0 && packet.SessionLow == 0) || packet.Connection == 0
        || packet.EventBytes.size() > MaxRemoteEventBytes || (packet.Kind == RemotePacketKind::Event) != (packet.EventBytes.empty() == false)) { return {}; }
    std::string result(4 + RemoteHeaderBytes + packet.EventBytes.size(), '\0');
    Store(result.data(), result.size() - 4, 4);
    result[4] = static_cast<char>(kind); result[5] = 1;
    const std::uint64_t fields[]{packet.SessionHigh, packet.SessionLow, packet.Connection, packet.Ordinal, packet.NetworkDropped, packet.ProducerDropped, packet.Sent};
    for (std::size_t index = 0; index < 7; ++index) { Store(result.data() + 12 + index * 8, fields[index], 8); }
    result.replace(4 + RemoteHeaderBytes, packet.EventBytes.size(), packet.EventBytes);
    return result;
}
RemotePacketParser::RemotePacketParser(std::function<bool(RemotePacket&&)> receive) : m_Receive(std::move(receive)) {}
bool RemotePacketParser::Feed(std::string_view bytes)
{
    if (m_Failed == true) { return false; }
    while (bytes.empty() == false)
    {
        const auto take = std::min(m_Expected - m_Pending.size(), bytes.size());
        m_Pending.append(bytes.data(), take); bytes.remove_prefix(take);
        if (m_Pending.size() != m_Expected) { continue; }
        if (m_Expected == 4)
        {
            const auto length = Load(m_Pending.data(), 4);
            if (length < RemoteHeaderBytes || length > MaxRemoteBodyBytes) { m_Failed = true; return false; }
            m_Expected = 4 + static_cast<std::size_t>(length);
        }
        else
        {
            if (Deliver() == false) { m_Failed = true; return false; }
            m_Pending.clear(); m_Expected = 4;
        }
    }
    return true;
}
bool RemotePacketParser::Deliver()
{
    const auto* body = m_Pending.data() + 4;
    if (body[1] != 1 || Load(body + 2, 6) != 0) { return false; }
    RemotePacket packet; packet.Kind = static_cast<RemotePacketKind>(body[0]);
    packet.SessionHigh = Load(body + 8, 8); packet.SessionLow = Load(body + 16, 8); packet.Connection = Load(body + 24, 8);
    packet.Ordinal = Load(body + 32, 8); packet.NetworkDropped = Load(body + 40, 8);
    packet.ProducerDropped = Load(body + 48, 8); packet.Sent = Load(body + 56, 8);
    packet.EventBytes.assign(body + RemoteHeaderBytes, m_Pending.size() - 4 - RemoteHeaderBytes);
    if (EncodeRemotePacket(packet).empty() == true || m_Receive == nullptr) { return false; }
    if (packet.Kind == RemotePacketKind::Event) { DiagnosticEvent event; if (DecodeRemoteEvent(packet, event) == false) { return false; } }
    return m_Receive(std::move(packet));
}
bool RemotePacketParser::Finish() const noexcept { return m_Failed == false && m_Pending.empty(); }
}
