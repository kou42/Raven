#ifdef _WIN32
#include "Raven/Network/Tests/WindowsTcpTests.h"
#include "Raven/Network/Windows/WindowsTcpSocket.h"
#include "Raven/Network/ControlMessageReceiver.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Raven
{
namespace Network
{
namespace Tests
{
namespace
{
TcpDeadline Deadline()
{
    return std::chrono::steady_clock::now() + std::chrono::seconds(3);
}
bool Pair(WindowsTcpSocket& client, WindowsTcpSocket& server, TcpError& error)
{
    WindowsTcpSocket listener;
    const auto deadline = Deadline();
    return listener.ListenLoopback(0, error)
        && client.ConnectLoopback(listener.GetLocalPort(), deadline, error)
        && listener.Accept(server, deadline, error);
}
bool ReadQueued(WindowsTcpSocket& socket, ControlMessageReceiver& receiver, TcpError& error)
{
    const auto deadline = Deadline();
    while (receiver.GetQueuedCount() == 0)
    {
        if (ReceiveControlStream(socket, receiver, deadline, error) == false || socket.IsOpen() == false)
        {
            return false;
        }
    }
    return true;
}
class Handler final : public IControlMessageHandler
{
public:
    std::vector<std::uint64_t> Requests;
    bool Ready = false;
    bool CorrectThread = true;
    std::thread::id Owner = std::this_thread::get_id();
    void OnPing(std::uint64_t request, const PingMessage&) override
    {
        CorrectThread = CorrectThread && Owner == std::this_thread::get_id();
        Requests.push_back(request);
    }
    void OnSetReady(std::uint64_t request, const SetReadyMessage& value) override
    {
        CorrectThread = CorrectThread && Owner == std::this_thread::get_id();
        Requests.push_back(request);
        Ready = value.Ready;
    }
};
}

int RunWindowsTcpTests()
{
    int checks = 0;
    int failures = 0;
    auto check = [&](bool success, const char* name)
    {
        ++checks;
        if (success == false)
        {
            ++failures;
            std::cerr << "FAIL TCP: " << name << '\n';
        }
    };
    TcpError error;
    MessageError codecError;
    const ControlMessage message{1, PingMessage{42}};
    std::string frame;
    check(ControlMessageCodec::EncodeStreamFrame(message, frame, codecError), "encode frame");

    // Loopback接続を作れない環境で残りを成功扱いにしない。
    WindowsTcpSocket client;
    WindowsTcpSocket server;
    if (Pair(client, server, error) == false)
    {
        check(false, "create real loopback socket pair");
        std::cerr << "TCP error=" << static_cast<int>(error.Code) << ", native=" << error.NativeCode << '\n';
        return 1;
    }
    check(client.IsOpen() && server.IsOpen() && client.GetLocalPort() != 0
        && server.GetLocalPort() != 0, "accepted sockets survive listener destruction");
    ControlMessageReceiver receiver(MessageTransport::Stream);
    check(client.SendAll(frame.substr(0, 2), Deadline(), error)
        && ReceiveControlStream(server, receiver, Deadline(), error)
        && receiver.GetQueuedCount() == 0, "partial prefix through real socket");
    check(client.SendAll(frame.substr(2, 3), Deadline(), error)
        && ReceiveControlStream(server, receiver, Deadline(), error)
        && receiver.GetQueuedCount() == 0, "partial body through real socket");
    check(client.SendAll(frame.substr(5), Deadline(), error)
        && ReadQueued(server, receiver, error), "complete fragmented frame");
    ControlMessage decoded;
    Handler handler;
    check(receiver.TryPop(decoded) && decoded.RequestId == 1
        && std::get<PingMessage>(decoded.Payload).Nonce == 42
        && ControlMessageCodec::Dispatch(decoded, handler, codecError), "real socket ping dispatch");
    std::string reply;
    check(ControlMessageCodec::EncodeStreamFrame({2, SetReadyMessage{true}}, reply, codecError)
        && server.SendAll(reply, Deadline(), error), "server response");
    ControlMessageReceiver response(MessageTransport::Stream);
    check(ReadQueued(client, response, error) && response.TryPop(decoded)
        && ControlMessageCodec::Dispatch(decoded, handler, codecError)
        && handler.Ready && handler.CorrectThread, "bidirectional typed round trip");
    check(client.SendAll(frame + reply, Deadline(), error), "coalesced frame send");
    const auto coalescedDeadline = Deadline();
    while (receiver.GetQueuedCount() < 2 && server.IsOpen())
    {
        if (ReceiveControlStream(server, receiver, coalescedDeadline, error) == false)
        {
            break;
        }
    }
    check(receiver.TryPop(decoded) && decoded.RequestId == 1
        && receiver.TryPop(decoded) && decoded.RequestId == 2, "coalesced socket FIFO");

    // Raw ReceiveのTimeoutはデータを消費せず再試行可。Control接続のTimeoutは後で終端を検証する。
    std::array<char, 8> scratch{};
    std::size_t count = 99;
    check(server.Receive(scratch.data(), scratch.size(), count,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(25), error) == false
        && error.Code == TcpErrorCode::Timeout && count == 0 && server.IsOpen(), "bounded idle receive");
    check(client.SendAll(frame, Deadline(), error) && ReadQueued(server, receiver, error)
        && receiver.TryPop(decoded), "raw timeout allows retry");
    check(client.ShutdownSend(error) && client.ShutdownSend(error), "idempotent send shutdown");
    check(client.SendAll(frame, Deadline(), error) == false && error.Code == TcpErrorCode::InvalidState,
        "send after shutdown rejected");
    check(ReceiveControlStream(server, receiver, Deadline(), error)
        && server.IsOpen() == false && receiver.GetState() == ReceiverState::Closed,
        "graceful peer EOF closes receiver");
    client.Close();
    client.Close();
    check(client.IsOpen() == false && client.GetLocalPort() == 0, "idempotent close");

    // Prefix/Bodyの代表的な途中位置でEOFを検証する。全位置のFramer検証は既存CPUテストで行う。
    // 各試行の接続は別のWSAStartup参照を所有する。
    for (const std::size_t cut : {std::size_t(1), std::size_t(3), std::size_t(4), std::size_t(5), frame.size() - 1})
    {
        WindowsTcpSocket sender;
        WindowsTcpSocket peer;
        if (Pair(sender, peer, error) == false)
        {
            check(false, "truncation socket pair");
            continue;
        }
        ControlMessageReceiver truncated(MessageTransport::Stream);
        bool sent = sender.SendAll(std::string_view(frame).substr(0, cut), Deadline(), error)
            && sender.ShutdownSend(error);
        const auto deadline = Deadline();
        bool accepted = true;
        while (peer.IsOpen() && accepted)
        {
            accepted = ReceiveControlStream(peer, truncated, deadline, error);
        }
        check(sent && accepted == false && error.Code == TcpErrorCode::Protocol
            && truncated.GetError().Code == MessageErrorCode::Truncated
            && peer.IsOpen() == false && truncated.GetQueuedCount() == 0, "truncated TCP stream rejected");
    }

    // 不正長・不正MessagePack・非対応Versionを実TCPで受信する。
    std::string badVersion = frame;
    const auto versionKey = badVersion.find("protocolVersion");
    if (versionKey != std::string::npos)
    {
        badVersion[versionKey + std::string("protocolVersion").size()] = 2;
    }
    const std::vector<std::string> malformed = {
        std::string(4, '\0'), std::string(4, '\xff'), std::string("\0\0\0\x01\xc1", 5), badVersion
    };
    for (const auto& bytes : malformed)
    {
        WindowsTcpSocket sender;
        WindowsTcpSocket peer;
        if (Pair(sender, peer, error) == false)
        {
            check(false, "malformed socket pair");
            continue;
        }
        ControlMessageReceiver invalid(MessageTransport::Stream);
        check(sender.SendAll(bytes, Deadline(), error), "send malformed bytes");
        bool accepted = true;
        const auto deadline = Deadline();
        while (accepted && peer.IsOpen())
        {
            accepted = ReceiveControlStream(peer, invalid, deadline, error);
        }
        check(accepted == false && error.Code == TcpErrorCode::Protocol
            && invalid.GetState() == ReceiverState::Failed && invalid.GetQueuedCount() == 0
            && peer.IsOpen() == false, "protocol failure closes real socket");
    }

    // 有効な未処理値と部分Frameを残して期限切れ。制御接続は両方を破棄する。
    {
        WindowsTcpSocket sender;
        WindowsTcpSocket peer;
        if (Pair(sender, peer, error))
        {
            ControlMessageReceiver pending(MessageTransport::Stream);
            check(sender.SendAll(frame + frame.substr(0, 2), Deadline(), error)
                && ReadQueued(peer, pending, error), "queue before message deadline");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
            bool received = true;
            while (received)
            {
                received = ReceiveControlStream(peer, pending, deadline, error);
            }
            check(error.Code == TcpErrorCode::Timeout && peer.IsOpen() == false
                && pending.GetState() == ReceiverState::Closed && pending.GetQueuedCount() == 0,
                "control deadline closes and purges pending data");
        }
        else
        {
            check(false, "deadline pair");
        }
    }

    // Socket所有権のMoveと、既存所有Socketへの代入時のCloseを検証する。
    {
        WindowsTcpSocket source;
        WindowsTcpSocket peer;
        WindowsTcpSocket replacement;
        WindowsTcpSocket replacedPeer;
        if (Pair(source, peer, error) && Pair(replacement, replacedPeer, error))
        {
            WindowsTcpSocket moved(std::move(source));
            check(source.IsOpen() == false && moved.IsOpen(), "move construction transfers socket");
            replacement = std::move(moved);
            check(moved.IsOpen() == false && replacement.IsOpen(), "move assignment transfers socket");
            count = 99;
            check(replacedPeer.Receive(scratch.data(), scratch.size(), count, Deadline(), error)
                && count == 0 && replacedPeer.IsOpen() == false, "move assignment closes previous socket");
            ControlMessageReceiver movedReceiver(MessageTransport::Stream);
            check(replacement.SendAll(frame, Deadline(), error) && ReadQueued(peer, movedReceiver, error),
                "moved socket remains usable");
        }
        else
        {
            check(false, "move socket pairs");
        }
    }

    WindowsTcpSocket listener;
    if (listener.ListenLoopback(0, error))
    {
        const auto port = listener.GetLocalPort();
        WindowsTcpSocket conflict;
        check(conflict.ListenLoopback(port, error) == false && conflict.IsOpen() == false
            && error.Code == TcpErrorCode::Bind && error.NativeCode != 0, "exclusive bind failure releases socket");
        WindowsTcpSocket accepted;
        const auto started = std::chrono::steady_clock::now();
        check(listener.Accept(accepted, started + std::chrono::milliseconds(25), error) == false
            && error.Code == TcpErrorCode::Timeout && accepted.IsOpen() == false && listener.IsOpen(),
            "bounded accept preserves listener and output");
        check(std::chrono::steady_clock::now() - started < std::chrono::seconds(2), "accept wait bounded");
        check(listener.SendAll(frame, Deadline(), error) == false && error.Code == TcpErrorCode::InvalidState,
            "listener cannot send");
        listener.Close();
        WindowsTcpSocket refused;
        check(refused.ConnectLoopback(port, Deadline(), error) == false && refused.IsOpen() == false
            && error.NativeCode != 0, "connect to closed loopback port fails and releases socket");
    }
    else
    {
        check(false, "listener for negative tests");
    }
    WindowsTcpSocket closed;
    check(closed.ConnectLoopback(0, Deadline(), error) == false && error.Code == TcpErrorCode::InvalidArgument,
        "zero remote port rejected");
    check(closed.ConnectLoopback(1, std::chrono::steady_clock::now(), error) == false
        && error.Code == TcpErrorCode::Timeout && closed.IsOpen() == false, "expired connect does not open socket");
    check(closed.Receive(scratch.data(), scratch.size(), count, Deadline(), error) == false
        && count == 0 && error.Code == TcpErrorCode::InvalidState, "closed receive rejected");

    {
        WindowsTcpSocket sender;
        WindowsTcpSocket peer;
        if (Pair(sender, peer, error))
        {
            count = 99;
            check(peer.Receive(nullptr, 1, count, Deadline(), error) == false
                && error.Code == TcpErrorCode::InvalidArgument && count == 0 && peer.IsOpen(),
                "invalid buffer does not close valid socket");
            check(peer.ConnectLoopback(sender.GetLocalPort(), Deadline(), error) == false
                && error.Code == TcpErrorCode::InvalidState && peer.IsOpen(),
                "connect preserves existing socket");
            check(sender.SendAll(frame, std::chrono::steady_clock::now(), error) == false
                && error.Code == TcpErrorCode::Timeout && sender.IsOpen() == false,
                "expired send deadline closes connection");
        }
        else
        {
            check(false, "send deadline pair");
        }
    }
    for (const bool policyFailure : {false, true})
    {
        WindowsTcpSocket sender;
        WindowsTcpSocket peer;
        if (Pair(sender, peer, error))
        {
            MessageReceiveLimits limits;
            limits.MaxQueuedMessages = policyFailure ? 64 : 0;
            ControlMessagePolicy policy;
            policy.AllowPing = policyFailure == false;
            ControlMessageReceiver rejected(MessageTransport::Stream, limits, policy);
            check(sender.SendAll(frame, Deadline(), error), "send for receiver rejection");
            bool accepted = true;
            const auto deadline = Deadline();
            while (accepted)
            {
                accepted = ReceiveControlStream(peer, rejected, deadline, error);
            }
            check(peer.IsOpen() == false && error.Code == TcpErrorCode::Protocol
                && rejected.GetState() == ReceiverState::Failed
                && rejected.GetError().Code == (policyFailure ? MessageErrorCode::UnsupportedMessage
                    : MessageErrorCode::QueueFull), "receiver rejection closes socket preserving reason");
        }
        else
        {
            check(false, "receiver rejection pair");
        }
    }

    // 複数Send/ReceiveにまたがるByte列を照合。受信Threadは期限内で終わり、Close前にjoinする。
    {
        WindowsTcpSocket sender;
        WindowsTcpSocket peer;
        if (Pair(sender, peer, error))
        {
            std::string payload(1024u * 1024u, '\0');
            for (std::size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<char>(i % 251u);
            }
            std::string received(payload.size(), '\0');
            bool read = true;
            const auto deadline = Deadline();
            std::thread reader([&]()
            {
                std::size_t offset = 0;
                TcpError local;
                while (offset < received.size())
                {
                    std::size_t bytes = 0;
                    const std::size_t capacity = (std::min)(std::size_t(8191), received.size() - offset);
                    if (peer.Receive(received.data() + offset, capacity, bytes, deadline, local) == false || bytes == 0)
                    {
                        read = false;
                        break;
                    }
                    offset += bytes;
                }
            });
            const bool sent = sender.SendAll(payload, deadline, error);
            reader.join();
            check(sent && read && received == payload, "large partial IO preserves exact bytes");
            sender.Close();
            peer.Close();
        }
        else
        {
            check(false, "large transfer pair");
        }
    }

    // 受信WorkerはSocketだけを操作し、所有Queueの値を呼び出し側ThreadでDispatchする。
    {
        WindowsTcpSocket sender;
        WindowsTcpSocket peer;
        if (Pair(sender, peer, error))
        {
            ControlMessageReceiver threaded(MessageTransport::Stream);
            constexpr std::uint64_t messages = 16;
            bool workerSuccess = true;
            const auto deadline = Deadline();
            std::thread worker([&]()
            {
                TcpError local;
                // この試験ではjoin後にConsumerを開始し、Frame数をQueueから正確に数える。
                // recvの呼び出し回数やTCPのChunk境界をMessage数として扱わない。
                while (threaded.GetQueuedCount() < messages)
                {
                    if (ReceiveControlStream(peer, threaded, deadline, local) == false || peer.IsOpen() == false)
                    {
                        workerSuccess = false;
                        break;
                    }
                }
            });
            bool sent = true;
            Handler mainHandler;
            for (std::uint64_t i = 1; i <= messages; ++i)
            {
                std::string bytes;
                if (ControlMessageCodec::EncodeStreamFrame({i, PingMessage{i}}, bytes, codecError) == false
                    || sender.SendAll(bytes, deadline, error) == false)
                {
                    sent = false;
                    break;
                }
            }
            worker.join();
            while (threaded.TryPop(decoded))
            {
                sent = ControlMessageCodec::Dispatch(decoded, mainHandler, codecError) && sent;
            }
            check(sent && workerSuccess && mainHandler.CorrectThread
                && mainHandler.Requests.size() == messages, "socket worker to main thread dispatch and join");
            bool ordered = true;
            for (std::size_t i = 0; i < mainHandler.Requests.size(); ++i)
            {
                ordered = ordered && mainHandler.Requests[i] == i + 1;
            }
            check(ordered, "socket worker FIFO");
            peer.Close();
            threaded.Disconnect();
        }
        else
        {
            check(false, "threaded pair");
        }
    }

    std::cout << "Windows TCP: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}
#endif
