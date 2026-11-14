#include "Raven/Network/ControlMessageCodec.h"
#include "Raven/Data/DataDocument.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include <new>
#include <stdexcept>
#include <utility>

namespace Raven
{
namespace Network
{
namespace
{
bool Fail(MessageError& error, MessageErrorCode code)
{
    error.Code = code;
    return false;
}
// Protocolの整数はFloat64からの丸めや暗黙変換を許可しない。
bool ReadInteger(Data::DataElement object, std::string_view key, std::uint64_t& output,
    Data::SerializationContext& context)
{
    Data::DataElement value;
    auto field = context.Field(key);
    if (object.TryFind(key, value) == false)
    {
        return field.Fail(Data::SerializationErrorCode::MissingField, object);
    }
    if (value.IsType(Data::DataType::Int64) == false && value.IsType(Data::DataType::UInt64) == false)
    {
        return field.Fail(Data::SerializationErrorCode::TypeMismatch, value);
    }
    if (value.TryGetUInt64(output) == false)
    {
        return field.Fail(Data::SerializationErrorCode::NumericRange, value);
    }
    return true;
}
bool HasMembers(Data::DataElement element, std::uint32_t expected)
{
    std::uint32_t count = 0;
    return element.IsType(Data::DataType::Object) && element.TryGetCount(count) && count == expected;
}
}

IControlMessageHandler::~IControlMessageHandler() = default;

bool ControlMessageCodec::IsAllowed(const ControlMessage& message, const ControlMessagePolicy& policy)
{
    return (std::holds_alternative<PingMessage>(message.Payload) && policy.AllowPing)
        || (std::holds_alternative<SetReadyMessage>(message.Payload) && policy.AllowSetReady);
}

bool ControlMessageCodec::Encode(const ControlMessage& message, std::string& output, MessageError& error)
{
    error = {};
    if (message.RequestId == 0)
    {
        return Fail(error, MessageErrorCode::InvalidEnvelope);
    }
    try
    {
        Data::DataWriteLimits limits;
        limits.MaxOutputBytes = MaxControlMessageBytes;
        Data::MessagePackWriter writer(limits);
        Data::SerializationContext context(error.Field);
        const bool ping = std::holds_alternative<PingMessage>(message.Payload);
        const auto type = ping ? ControlMessageType::Ping : ControlMessageType::SetReady;
        if (context.Written(writer.BeginObject(), writer) == false
            || Data::WriteField(writer, "protocolVersion", ControlProtocolVersion, context) == false
            || Data::WriteField(writer, "messageType", static_cast<std::uint32_t>(type), context) == false
            || Data::WriteField(writer, "requestId", message.RequestId, context) == false
            || context.Written(writer.WriteKey("payload"), writer) == false
            || context.Written(writer.BeginObject(), writer) == false)
        {
            return Fail(error, MessageErrorCode::InvalidPayload);
        }
        if (ping)
        {
            if (Data::WriteField(writer, "nonce", std::get<PingMessage>(message.Payload).Nonce, context) == false)
            {
                return Fail(error, MessageErrorCode::InvalidPayload);
            }
        }
        else if (Data::WriteField(writer, "ready", std::get<SetReadyMessage>(message.Payload).Ready, context) == false)
        {
            return Fail(error, MessageErrorCode::InvalidPayload);
        }
        if (context.Written(writer.EndObject(), writer) == false
            || context.Written(writer.EndObject(), writer) == false || writer.Finish(output) == false)
        {
            return Fail(error, MessageErrorCode::InvalidPayload);
        }
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return Fail(error, MessageErrorCode::OutOfMemory);
    }
    catch (const std::length_error&)
    {
        return Fail(error, MessageErrorCode::SizeLimit);
    }
}

bool ControlMessageCodec::Decode(std::string_view bytes, ControlMessage& output, MessageError& error)
{
    error = {};
    if (bytes.empty() || bytes.size() > MaxControlMessageBytes)
    {
        return Fail(error, MessageErrorCode::SizeLimit);
    }
    Data::DataLimits limits;
    limits.MaxInputBytes = MaxControlMessageBytes;
    limits.MaxTokens = 32;
    limits.MaxDepth = 4;
    limits.MaxContainerElements = 8;
    Data::DataDocument document;
    if (Data::MessagePackParser::Parse(bytes, document, &error.Parse, limits) == false)
    {
        return Fail(error, error.Parse.Code == Data::DataErrorCode::OutOfMemory
            ? MessageErrorCode::OutOfMemory : MessageErrorCode::ParseFailed);
    }
    const auto root = document.GetRoot();
    if (HasMembers(root, 4) == false)
    {
        return Fail(error, MessageErrorCode::InvalidEnvelope);
    }
    Data::SerializationContext context(error.Field);
    std::uint64_t version = 0;
    std::uint64_t type = 0;
    ControlMessage candidate;
    if (ReadInteger(root, "protocolVersion", version, context) == false)
    {
        return Fail(error, MessageErrorCode::InvalidEnvelope);
    }
    if (version != ControlProtocolVersion)
    {
        return Fail(error, MessageErrorCode::UnsupportedVersion);
    }
    if (ReadInteger(root, "messageType", type, context) == false
        || ReadInteger(root, "requestId", candidate.RequestId, context) == false || candidate.RequestId == 0)
    {
        return Fail(error, MessageErrorCode::InvalidEnvelope);
    }
    Data::DataElement payload;
    if (root.TryFind("payload", payload) == false || HasMembers(payload, 1) == false)
    {
        return Fail(error, MessageErrorCode::InvalidPayload);
    }
    auto payloadContext = context.Field("payload");
    if (type == static_cast<std::uint32_t>(ControlMessageType::Ping))
    {
        PingMessage ping;
        if (ReadInteger(payload, "nonce", ping.Nonce, payloadContext) == false)
        {
            return Fail(error, MessageErrorCode::InvalidPayload);
        }
        candidate.Payload = ping;
    }
    else if (type == static_cast<std::uint32_t>(ControlMessageType::SetReady))
    {
        SetReadyMessage ready;
        Data::ObjectReader object(payload, payloadContext);
        if (object.Required("ready", ready.Ready) == false)
        {
            return Fail(error, MessageErrorCode::InvalidPayload);
        }
        candidate.Payload = ready;
    }
    else
    {
        return Fail(error, MessageErrorCode::UnsupportedMessage);
    }
    // 所有型は整数・Boolのみ。Document/受信Buffer破棄後も安全で、Commitは確保を伴わない。
    output = candidate;
    return true;
}

bool ControlMessageCodec::EncodeStreamFrame(const ControlMessage& message, std::string& output, MessageError& error)
{
    std::string payload;
    if (Encode(message, payload, error) == false)
    {
        return false;
    }
    try
    {
        const auto length = static_cast<std::uint32_t>(payload.size());
        std::string frame;
        frame.reserve(4 + payload.size());
        for (unsigned i = 0; i < 4; ++i)
        {
            frame.push_back(static_cast<char>((length >> (24u - 8u * i)) & 255u));
        }
        frame.append(payload);
        output.swap(frame);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return Fail(error, MessageErrorCode::OutOfMemory);
    }
    catch (const std::length_error&)
    {
        return Fail(error, MessageErrorCode::SizeLimit);
    }
}

bool ControlMessageCodec::EncodeDatagram(const ControlMessage& message, std::string& output, MessageError& error)
{
    std::string bytes;
    if (Encode(message, bytes, error) == false)
    {
        return false;
    }
    if (bytes.size() > MaxControlDatagramBytes)
    {
        return Fail(error, MessageErrorCode::SizeLimit);
    }
    output.swap(bytes);
    return true;
}

bool ControlMessageCodec::Dispatch(const ControlMessage& message, IControlMessageHandler& handler,
    MessageError& error, const ControlMessagePolicy& policy)
{
    error = {};
    if (message.RequestId == 0)
    {
        return Fail(error, MessageErrorCode::InvalidEnvelope);
    }
    if (IsAllowed(message, policy) == false)
    {
        return Fail(error, MessageErrorCode::UnsupportedMessage);
    }
    const auto* ping = std::get_if<PingMessage>(&message.Payload);
    if (ping != nullptr)
    {
        handler.OnPing(message.RequestId, *ping);
    }
    else
    {
        handler.OnSetReady(message.RequestId, std::get<SetReadyMessage>(message.Payload));
    }
    return true;
}
}
}
