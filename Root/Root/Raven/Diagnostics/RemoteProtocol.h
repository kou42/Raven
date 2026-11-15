#pragma once
#include "Raven/Diagnostics/BinaryTrace.h"
#include <functional>
#include <string_view>

namespace Raven::Diagnostics
{
constexpr std::size_t MaxRemoteEventBytes = 1244;
constexpr std::size_t RemoteHeaderBytes = 64;
constexpr std::size_t MaxRemoteBodyBytes = RemoteHeaderBytes + MaxRemoteEventBytes;
enum class RemotePacketKind : std::uint8_t { Hello = 1, Event, Status, Close };
struct RemotePacket
{
    RemotePacketKind Kind = RemotePacketKind::Status;
    std::uint64_t SessionHigh = 0, SessionLow = 0, Connection = 0;
    std::uint64_t Ordinal = 0, NetworkDropped = 0, ProducerDropped = 0, Sent = 0;
    std::string EventBytes;
};
std::string EncodeRemoteEvent(const DiagnosticEvent& event);
bool DecodeRemoteEvent(const RemotePacket& packet, DiagnosticEvent& event);
std::string EncodeRemotePacket(const RemotePacket& packet);

// 接続ごとに生成し、受信Workerだけで使用します。Prefix検証前に外部長の確保はしません。
class RemotePacketParser final
{
public:
    explicit RemotePacketParser(std::function<bool(RemotePacket&&)> receive);
    bool Feed(std::string_view bytes);
    bool Finish() const noexcept;
private:
    bool Deliver();
    std::function<bool(RemotePacket&&)> m_Receive;
    std::string m_Pending;
    std::size_t m_Expected = 4;
    bool m_Failed = false;
};
}
