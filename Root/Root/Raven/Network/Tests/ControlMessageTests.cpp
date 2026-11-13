#include "Raven/Network/Tests/ControlMessageTests.h"
#include "Raven/Network/ControlMessageReceiver.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <atomic>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace Raven
{
namespace Network
{
namespace Tests
{
namespace
{
class Handler final : public IControlMessageHandler
{
public:
    std::vector<std::uint64_t> Requests;
    std::thread::id Thread;
    bool CorrectThread = true;
    bool Ready = false;
    std::uint64_t Nonce = 0;
    void OnPing(std::uint64_t request, const PingMessage& ping) override
    {
        CorrectThread = CorrectThread && std::this_thread::get_id() == Thread;
        Requests.push_back(request);
        Nonce = ping.Nonce;
    }
    void OnSetReady(std::uint64_t request, const SetReadyMessage& ready) override
    {
        CorrectThread = CorrectThread && std::this_thread::get_id() == Thread;
        Requests.push_back(request);
        Ready = ready.Ready;
    }
};
bool Same(const ControlMessage& a, const ControlMessage& b)
{
    if (a.RequestId != b.RequestId || a.Payload.index() != b.Payload.index())
    {
        return false;
    }
    const auto* ping = std::get_if<PingMessage>(&a.Payload);
    if (ping != nullptr)
    {
        return ping->Nonce == std::get<PingMessage>(b.Payload).Nonce;
    }
    return std::get<SetReadyMessage>(a.Payload).Ready == std::get<SetReadyMessage>(b.Payload).Ready;
}
std::string Frame(std::string_view body)
{
    std::string result;
    const auto size = static_cast<std::uint32_t>(body.size());
    for (unsigned i = 0; i < 4; ++i)
    {
        result.push_back(static_cast<char>((size >> (24u - 8u * i)) & 255u));
    }
    result.append(body);
    return result;
}
bool FromJson(std::string_view json, std::string& bytes)
{
    Data::DataDocument document;
    return Data::JsonParser::Parse(json, document) && Data::MessagePackWriter::Write(document, bytes);
}
}

int RunControlMessageTests()
{
    int checks = 0;
    int failures = 0;
    auto check = [&](bool value, const char* label)
    {
        ++checks;
        if (value == false)
        {
            ++failures;
            std::cerr << "FAIL Network: " << label << '\n';
        }
    };
    MessageError error;
    const ControlMessage ping{(std::numeric_limits<std::uint64_t>::max)(),
        PingMessage{(std::numeric_limits<std::uint64_t>::max)()}};
    const ControlMessage ready{2, SetReadyMessage{true}};
    ControlMessage decoded{99, SetReadyMessage{false}};
    std::string pingBytes;
    std::string readyBytes;
    check(ControlMessageCodec::Encode(ping, pingBytes, error), "encode full-width integers");
    check(ControlMessageCodec::Decode(pingBytes, decoded, error) && Same(decoded, ping), "ping round trip");
    check(ControlMessageCodec::Encode(ready, readyBytes, error)
        && ControlMessageCodec::Decode(readyBytes, decoded, error) && Same(decoded, ready), "ready round trip");
    std::string repeated;
    check(ControlMessageCodec::Encode(ping, repeated, error) && repeated == pingBytes, "deterministic encoding");
    const ControlMessage notReady{3, SetReadyMessage{false}};
    check(ControlMessageCodec::Encode(notReady, repeated, error)
        && ControlMessageCodec::Decode(repeated, decoded, error) && Same(decoded, notReady), "false bool preserved");
    repeated = "keep";
    check(ControlMessageCodec::Encode({}, repeated, error) == false && repeated == "keep",
        "zero request preserves encode output");

    std::string pingFrame;
    std::string readyFrame;
    check(ControlMessageCodec::EncodeStreamFrame(ping, pingFrame, error) && pingFrame == Frame(pingBytes),
        "stream big-endian prefix");
    check(ControlMessageCodec::EncodeStreamFrame(ready, readyFrame, error), "ready frame");
    check(ControlMessageCodec::EncodeDatagram(ready, repeated, error) && repeated == readyBytes,
        "datagram has no prefix");
    for (std::size_t split = 0; split <= pingFrame.size(); ++split)
    {
        ControlMessageReceiver receiver(MessageTransport::Stream);
        check(receiver.ReceiveStream(std::string_view(pingFrame).substr(0, split))
            && receiver.ReceiveStream(std::string_view(pingFrame).substr(split))
            && receiver.GetQueuedCount() == 1 && receiver.GetQueuedBytes() == pingBytes.size()
            && receiver.TryPop(decoded) && Same(decoded, ping) && receiver.GetQueuedBytes() == 0,
            "every stream split preserves message");
        const auto previous = decoded;
        check(receiver.TryPop(decoded) == false && Same(decoded, previous), "empty pop preserves output");
    }
    ControlMessageReceiver bytewise(MessageTransport::Stream);
    bool bytewiseSuccess = true;
    for (const char& byte : pingFrame)
    {
        bytewiseSuccess = bytewiseSuccess && bytewise.ReceiveStream({&byte, 1});
    }
    check(bytewiseSuccess && bytewise.TryPop(decoded) && Same(decoded, ping), "one byte chunks");
    for (std::size_t cut = 1; cut < pingFrame.size(); ++cut)
    {
        ControlMessageReceiver receiver(MessageTransport::Stream);
        check(receiver.ReceiveStream(readyFrame + pingFrame.substr(0, cut))
            && receiver.FinishStream() == false && receiver.GetError().Code == MessageErrorCode::Truncated
            && receiver.GetState() == ReceiverState::Failed && receiver.GetQueuedCount() == 0
            && receiver.GetQueuedBytes() == 0 && receiver.TryPop(decoded) == false,
            "EOF drops partial frame and queued messages");
    }

    Handler handler;
    handler.Thread = std::this_thread::get_id();
    ControlMessageReceiver loopback(MessageTransport::Stream);
    check(loopback.ReceiveStream(pingFrame + readyFrame) && loopback.GetQueuedCount() == 2,
        "coalesced loopback frames");
    check(loopback.TryPop(decoded) && ControlMessageCodec::Dispatch(decoded, handler, error)
        && handler.Nonce == std::get<PingMessage>(ping.Payload).Nonce, "dispatch ping");
    check(loopback.TryPop(decoded) && ControlMessageCodec::Dispatch(decoded, handler, error)
        && handler.Ready && handler.Requests == std::vector<std::uint64_t>({ping.RequestId, 2}),
        "dispatch order");
    check(loopback.FinishStream() && loopback.GetState() == ReceiverState::Closed
        && loopback.ReceiveStream(pingFrame) == false, "graceful EOF closes session");
    ControlMessageReceiver disconnect(MessageTransport::Stream);
    check(disconnect.ReceiveStream(pingFrame), "queue before disconnect");
    disconnect.Disconnect();
    disconnect.Disconnect();
    check(disconnect.GetState() == ReceiverState::Closed && disconnect.GetQueuedCount() == 0
        && disconnect.GetQueuedBytes() == 0 && disconnect.TryPop(decoded) == false,
        "disconnect is idempotent and purges queue");

    // JSON Fixtureを既存WriterでMessagePack化し、Wire型・必須・未知フィールドを検証する。
    const std::vector<std::string> malformed = {
        "null", "[]", "{}",
        R"({"protocolVersion":2,"messageType":1,"requestId":1,"payload":{"nonce":0}})",
        R"({"protocolVersion":1.0,"messageType":1,"requestId":1,"payload":{"nonce":0}})",
        R"({"protocolVersion":1,"messageType":99,"requestId":1,"payload":{"nonce":0}})",
        R"({"protocolVersion":1,"messageType":1.0,"requestId":1,"payload":{"nonce":0}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":0,"payload":{"nonce":0}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":-1,"payload":{"nonce":0}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":1.0,"payload":{"nonce":0}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":true,"payload":{"nonce":0}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":1,"payload":{"nonce":-1}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":1,"payload":{"nonce":1.0}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":1,"payload":{"nonce":"0"}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":1,"payload":{"ready":true}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":1,"payload":{"nonce":0,"extra":0}})",
        R"({"protocolVersion":1,"messageType":1,"requestId":1,"payload":null})",
        R"({"protocolVersion":1,"messageType":2,"requestId":1,"payload":{"ready":1}})",
        R"({"protocolVersion":1,"messageType":2,"requestId":1,"payload":{"ready":null}})",
        R"({"protocolVersion":1,"messageType":2,"requestId":1,"payload":{"ready":"true"}})",
        R"({"protocolVersion":1,"messageType":2,"requestId":1,"payload":{}})",
        R"({"protocolVersion":1,"messageType":2,"requestId":1,"payload":{"ready":true},"extra":1})",
        R"({"protocolVersion":1,"messageType":2,"wrongId":1,"payload":{"ready":true}})"
    };
    for (const auto& json : malformed)
    {
        std::string bytes;
        check(FromJson(json, bytes), "build malformed wire fixture");
        const auto previous = decoded;
        check(ControlMessageCodec::Decode(bytes, decoded, error) == false && Same(decoded, previous),
            "invalid envelope preserves decode output");
        ControlMessageReceiver receiver(MessageTransport::Stream);
        const auto count = handler.Requests.size();
        check(receiver.ReceiveStream(pingFrame + Frame(bytes)) == false
            && receiver.GetState() == ReceiverState::Failed && receiver.GetQueuedCount() == 0
            && receiver.TryPop(decoded) == false && handler.Requests.size() == count,
            "invalid wire never dispatches and purges earlier frames");
    }
    std::string diagnostic;
    check(FromJson(R"({"protocolVersion":2,"messageType":1,"requestId":1,"payload":{"nonce":0}})", diagnostic)
        && ControlMessageCodec::Decode(diagnostic, decoded, error) == false
        && error.Code == MessageErrorCode::UnsupportedVersion, "version mismatch diagnostic");
    check(FromJson(R"({"protocolVersion":1,"messageType":99,"requestId":1,"payload":{"nonce":0}})", diagnostic)
        && ControlMessageCodec::Decode(diagnostic, decoded, error) == false
        && error.Code == MessageErrorCode::UnsupportedMessage, "unknown type diagnostic");
    check(FromJson(R"({"protocolVersion":1,"messageType":2,"requestId":1,"payload":{"ready":1}})", diagnostic)
        && ControlMessageCodec::Decode(diagnostic, decoded, error) == false
        && error.Code == MessageErrorCode::InvalidPayload
        && std::string(error.Field.Path.data()) == "/payload/ready", "payload field diagnostic path");
    // Writerは重複キーを拒否するため、MapをWire上で直接構成してParser側も検証する。
    std::string duplicate(1, static_cast<char>(0x82));
    duplicate.append("\xa1" "x\x01\xa1" "x\x02", 6);
    check(ControlMessageCodec::Decode(duplicate, decoded, error) == false
        && error.Parse.Code == Data::DataErrorCode::DuplicateKey, "duplicate wire keys rejected");
    for (std::size_t cut = 0; cut < pingBytes.size(); ++cut)
    {
        const auto previous = decoded;
        check(ControlMessageCodec::Decode(std::string_view(pingBytes).substr(0, cut), decoded, error) == false
            && Same(decoded, previous), "message truncation preserves output");
    }
    check(ControlMessageCodec::Decode(pingBytes + readyBytes, decoded, error) == false,
        "concatenated messages rejected without framing");
    for (const std::string bytes : {std::string(1, '\xc1'), std::string("\xd4\x00\x01", 3),
        std::string(MaxControlMessageBytes + 1, 'x')})
    {
        check(ControlMessageCodec::Decode(bytes, decoded, error) == false, "unsupported or oversized wire");
    }

    const std::vector<std::string> badPrefixes = {
        std::string(4, '\0'), std::string(4, '\xff'), std::string("\0\0\x10\x01", 4)
    };
    for (const auto& prefix : badPrefixes)
    {
        ControlMessageReceiver receiver(MessageTransport::Stream);
        check(receiver.ReceiveStream(prefix) == false && receiver.GetError().Code == MessageErrorCode::SizeLimit
            && receiver.ReceiveStream(pingFrame) == false, "zero and oversized lengths fail before body");
        receiver.Disconnect();
        check(receiver.GetState() == ReceiverState::Failed
            && receiver.GetError().Code == MessageErrorCode::SizeLimit, "disconnect preserves first error");
    }
    ControlMessageReceiver maximumLength(MessageTransport::Stream);
    check(maximumLength.ReceiveStream(std::string("\0\0\x10\0", 4))
        && maximumLength.FinishStream() == false
        && maximumLength.GetError().Code == MessageErrorCode::Truncated,
        "maximum frame length accepted pending body");
    ControlMessageReceiver reusableCapacity(MessageTransport::Stream, {1, pingBytes.size()});
    check(reusableCapacity.ReceiveStream(pingFrame) && reusableCapacity.TryPop(decoded)
        && reusableCapacity.ReceiveStream(pingFrame) && reusableCapacity.TryPop(decoded),
        "pop restores count and byte budgets");
    MessageReceiveLimits one;
    one.MaxQueuedMessages = 1;
    ControlMessageReceiver countLimited(MessageTransport::Stream, one);
    check(countLimited.ReceiveStream(pingFrame + readyFrame) == false
        && countLimited.GetError().Code == MessageErrorCode::QueueFull && countLimited.GetQueuedCount() == 0,
        "queue count backpressure closes and clears");
    MessageReceiveLimits bytesLimit;
    bytesLimit.MaxQueuedBytes = pingBytes.size();
    ControlMessageReceiver byteLimited(MessageTransport::Stream, bytesLimit);
    check(byteLimited.ReceiveStream(pingFrame) && byteLimited.GetQueuedBytes() == pingBytes.size(),
        "queue exact byte budget");
    check(byteLimited.ReceiveStream(readyFrame) == false && byteLimited.GetQueuedBytes() == 0,
        "queue byte overflow clears");
    MessageReceiveLimits zero;
    zero.MaxQueuedMessages = 0;
    ControlMessageReceiver disabled(MessageTransport::Stream, zero);
    check(disabled.ReceiveStream(pingFrame) == false && disabled.GetError().Code == MessageErrorCode::QueueFull,
        "zero capacity queue");

    ControlMessagePolicy pingOnly;
    pingOnly.AllowSetReady = false;
    ControlMessageReceiver restricted(MessageTransport::Datagram, {}, pingOnly);
    check(restricted.ReceiveDatagram(pingBytes), "allowed message queued");
    check(restricted.ReceiveDatagram(readyBytes) == false && restricted.GetQueuedCount() == 0,
        "receiver policy rejects before enqueue");
    const auto calls = handler.Requests.size();
    check(ControlMessageCodec::Dispatch(ready, handler, error, pingOnly) == false
        && handler.Requests.size() == calls, "dispatch policy prevents handler call");
    check(ControlMessageCodec::Dispatch({}, handler, error) == false
        && handler.Requests.size() == calls, "invalid local message cannot dispatch");

    ControlMessageReceiver datagrams(MessageTransport::Datagram);
    std::string temporary = readyBytes;
    check(datagrams.ReceiveDatagram(temporary), "datagram receive");
    temporary.assign(temporary.size(), 'x');
    check(datagrams.TryPop(decoded) && Same(decoded, ready), "queue owns decoded value");
    for (const auto& bytes : {std::string(), std::string(MaxControlDatagramBytes + 1, 'x'), pingBytes + readyBytes})
    {
        ControlMessageReceiver receiver(MessageTransport::Datagram);
        check(receiver.ReceiveDatagram(bytes) == false && receiver.GetState() == ReceiverState::Failed,
            "datagram empty oversized concatenated rejection");
    }
    ControlMessageReceiver datagramBoundary(MessageTransport::Datagram);
    check(datagramBoundary.ReceiveDatagram(std::string(MaxControlDatagramBytes, 'x')) == false
        && datagramBoundary.GetError().Code == MessageErrorCode::ParseFailed,
        "maximum datagram reaches codec rather than size rejection");
    ControlMessageReceiver wrongStream(MessageTransport::Datagram);
    ControlMessageReceiver wrongDatagram(MessageTransport::Stream);
    check(wrongStream.ReceiveStream(pingFrame) == false
        && wrongStream.GetError().Code == MessageErrorCode::WrongTransport, "stream transport mismatch");
    check(wrongDatagram.ReceiveDatagram(pingBytes) == false
        && wrongDatagram.GetError().Code == MessageErrorCode::WrongTransport, "datagram transport mismatch");

    // 本番Socketの代わりに受信Workerから所有Queueへ投入し、呼び出し側ThreadでDispatchする。
    constexpr std::uint64_t threadedMessages = 256;
    MessageReceiveLimits threadedLimits;
    threadedLimits.MaxQueuedMessages = static_cast<std::size_t>(threadedMessages);
    ControlMessageReceiver threaded(MessageTransport::Stream, threadedLimits);
    std::atomic<bool> done{false};
    std::atomic<bool> workerSuccess{true};
    std::thread producer([&]()
    {
        for (std::uint64_t i = 1; i <= threadedMessages; ++i)
        {
            std::string frame;
            MessageError local;
            if (ControlMessageCodec::EncodeStreamFrame({i, PingMessage{i}}, frame, local) == false
                || threaded.ReceiveStream(frame.substr(0, 3)) == false
                || threaded.ReceiveStream(frame.substr(3)) == false)
            {
                workerSuccess = false;
                break;
            }
        }
        done = true;
    });
    Handler threadedHandler;
    threadedHandler.Thread = std::this_thread::get_id();
    bool dispatched = true;
    while (done.load() == false || threaded.GetQueuedCount() != 0)
    {
        ControlMessage next;
        if (threaded.TryPop(next))
        {
            dispatched = ControlMessageCodec::Dispatch(next, threadedHandler, error) && dispatched;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();
    check(workerSuccess && dispatched && threadedHandler.CorrectThread
        && threadedHandler.Requests.size() == threadedMessages, "worker to caller thread dispatch");
    bool ordered = true;
    for (std::size_t i = 0; i < threadedHandler.Requests.size(); ++i)
    {
        ordered = ordered && threadedHandler.Requests[i] == i + 1;
    }
    check(ordered && threaded.GetQueuedBytes() == 0, "threaded FIFO and byte accounting");
    threaded.Disconnect();
    check(threaded.ReceiveStream(pingFrame) == false, "disconnected session cannot be reused");

    std::cout << "Network Phase11: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
}
}
}
