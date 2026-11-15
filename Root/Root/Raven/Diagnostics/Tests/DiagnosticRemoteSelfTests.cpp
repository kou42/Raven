#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Diagnostics/RemoteDiagnostics.h"
#include "Raven/Diagnostics/RemoteProtocol.h"
#include "Raven/Diagnostics/DiagnosticEventStore.h"
#include "Raven/Network/Windows/WindowsTcpSocket.h"
#include <chrono>
#include <array>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace Raven::tests
{
namespace
{
void CheckRemote(bool condition, std::source_location where = std::source_location::current())
{
    if (condition == false) { throw std::runtime_error(std::string(where.file_name()) + ':' + std::to_string(where.line())); }
}
}
void RunDiagnosticRemoteSelfTests()
{
    using namespace Diagnostics;
    DiagnosticEvent event; event.Id = 10; event.Payload = std::string("owned");
    RemotePacket packet; packet.Kind = RemotePacketKind::Event; packet.SessionHigh = 1; packet.Connection = 1;
    packet.Ordinal = 2; packet.EventBytes = EncodeRemoteEvent(event);
    const auto encoded = EncodeRemotePacket(packet);
    CheckRemote(encoded.size() <= 4 + MaxRemoteBodyBytes && encoded[4] == 2 && encoded[5] == 1 && encoded[12] == 1);
    for (std::size_t split = 0; split <= encoded.size(); ++split)
    {
        std::size_t count = 0;
        RemotePacketParser parser([&](RemotePacket&& received)
        {
            DiagnosticEvent decoded; CheckRemote(DecodeRemoteEvent(received, decoded));
            CheckRemote(std::get<std::string>(decoded.Payload) == "owned" && received.Ordinal == 2); ++count; return true;
        });
        CheckRemote(parser.Feed(std::string_view(encoded).substr(0, split)));
        CheckRemote(parser.Feed(std::string_view(encoded).substr(split)) && parser.Finish() && count == 1);
    }
    for (std::size_t length = 1; length < encoded.size(); ++length)
    {
        RemotePacketParser parser([](RemotePacket&&) { return true; });
        CheckRemote(parser.Feed(std::string_view(encoded).substr(0, length)) && parser.Finish() == false);
    }
    for (const auto offset : {3u, 4u, 5u, 6u, 68u})
    {
        auto bad = encoded; bad[offset] = static_cast<char>(255);
        RemotePacketParser parser([](RemotePacket&&) { return true; });
        CheckRemote(parser.Feed(bad) == false && parser.Finish() == false);
    }
    std::size_t coalesced = 0;
    RemotePacketParser parser([&](RemotePacket&&) { ++coalesced; return true; });
    CheckRemote(parser.Feed(encoded + encoded) && coalesced == 2 && parser.Finish());
#ifdef _WIN32
    using namespace std::chrono_literals;
    Network::WindowsTcpSocket listener; Network::TcpError error;
    CheckRemote(listener.ListenLoopback(0, error)); const auto port = listener.GetLocalPort();
    // Peer未接続で飽和させてもCollectorは通信を待たず、件数/byteの上限を保ちます。
    NetworkDiagnosticSink sink({port, 2, MaxRemoteEventBytes * 2});
    for (int index = 0; index < 10000; ++index) { CheckRemote(sink.Write(event)); }
    auto statistics = sink.GetStatistics();
    CheckRemote(statistics.QueuedEvents <= 2 && statistics.QueuedBytes <= MaxRemoteEventBytes * 2 && statistics.Dropped > 0);
    // 未読Peerの存在中でも、StopはI/O期限と短いdrain猶予で終了します。
    const auto stopBegin = std::chrono::steady_clock::now(); sink.Stop();
    CheckRemote(std::chrono::steady_clock::now() - stopBegin < 2s);
    statistics = sink.GetStatistics(); CheckRemote(statistics.QueuedEvents == 0 && statistics.QueuedBytes == 0);
    // 接続拒否からの復旧。Socketは各Threadだけが生成・操作・破棄します。
    listener.Close();
    NetworkDiagnosticSink reconnect({port});
    const auto failureDeadline = std::chrono::steady_clock::now() + 2s;
    while (reconnect.GetStatistics().ConnectionFailures == 0 && std::chrono::steady_clock::now() < failureDeadline)
    { std::this_thread::sleep_for(5ms); }
    CheckRemote(reconnect.GetStatistics().ConnectionFailures > 0);
    CheckRemote(listener.ListenLoopback(port, error));
    Network::WindowsTcpSocket peer;
    CheckRemote(listener.Accept(peer, std::chrono::steady_clock::now() + 2s, error));
    CheckRemote(reconnect.Write(event));
    std::size_t received = 0; bool hello = false;
    RemotePacketParser incoming([&](RemotePacket&& value)
    {
        if (value.Kind == RemotePacketKind::Hello) { hello = true; }
        if (value.Kind == RemotePacketKind::Event) { ++received; }
        return true;
    });
    std::array<char, 512> bytes{}; const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (received == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::size_t count = 0; CheckRemote(peer.Receive(bytes.data(), bytes.size(), count, deadline, error));
        CheckRemote(count > 0 && incoming.Feed({bytes.data(), count}));
    }
    reconnect.Stop(); CheckRemote(hello && received == 1 && reconnect.GetStatistics().ConnectionFailures > 0);
    // 切断中のNetwork Queueが満杯でも、同じCollectorのローカルSinkは通常どおり進みます。
    listener.Close(); peer.Close();
    auto local = std::make_shared<DiagnosticEventStore>();
    const auto dropsBefore = GetAsyncDroppedCount();
    {
        std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
        sinks.push_back(std::make_unique<NetworkDiagnosticSink>(RemoteSinkOptions{port, 2, MaxRemoteEventBytes * 2}));
        sinks.push_back(std::make_unique<DiagnosticStoreSink>(local));
        DiagnosticSession session(std::move(sinks)); CheckRemote(session.IsActive());
        for (int index = 0; index < 1000; ++index) { Emit(10, DiagnosticCategory::Core, DiagnosticSeverity::Info, std::int64_t(index)); }
        FlushDiagnostics();
    }
    CheckRemote(local->TrySnapshot(0)->Events.size() + GetAsyncDroppedCount() - dropsBefore == 1000);
#endif
}
}
