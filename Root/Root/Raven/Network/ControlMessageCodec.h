#pragma once

#include "Raven/Data/Serialization/Serialization.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace Raven
{
namespace Network
{
// Phase 11の制御通信用の試験Protocol。認証・接続・配送の保証は通信層の責務。
inline constexpr std::uint32_t ControlProtocolVersion = 1;
inline constexpr std::uint32_t MaxControlMessageBytes = 4096;
inline constexpr std::uint32_t MaxControlDatagramBytes = 1200;

enum class ControlMessageType : std::uint32_t { Ping = 1, SetReady = 2 };
struct PingMessage { std::uint64_t Nonce = 0; };
struct SetReadyMessage { bool Ready = false; };
struct ControlMessage
{
    std::uint64_t RequestId = 0;
    std::variant<PingMessage, SetReadyMessage> Payload;
};

enum class MessageErrorCode
{
    None, SizeLimit, InvalidEnvelope, UnsupportedVersion, UnsupportedMessage,
    InvalidPayload, ParseFailed, OutOfMemory, QueueFull, Truncated, Closed, WrongTransport
};
struct MessageError
{
    MessageErrorCode Code = MessageErrorCode::None;
    Data::DataError Parse;
    Data::SerializationError Field;
};
struct ControlMessagePolicy
{
    bool AllowPing = true;
    bool AllowSetReady = true;
};

// Handlerは所有しない。Dispatch呼び出し中だけ生存が必要で、呼び出したThread上で実行する。
class IControlMessageHandler
{
public:
    virtual ~IControlMessageHandler();
    virtual void OnPing(std::uint64_t requestId, const PingMessage& value) = 0;
    virtual void OnSetReady(std::uint64_t requestId, const SetReadyMessage& value) = 0;
};

class ControlMessageCodec
{
public:
    // 一時Buffer/Documentで全検証後だけoutputを置換。入力Viewを保持しない。
    static bool Encode(const ControlMessage& message, std::string& output, MessageError& error);
    static bool Decode(std::string_view bytes, ControlMessage& output, MessageError& error);
    // Stream PrefixはPayload長を表す4 byte big-endian。DatagramにPrefixは付けない。
    static bool EncodeStreamFrame(const ControlMessage& message, std::string& output, MessageError& error);
    static bool EncodeDatagram(const ControlMessage& message, std::string& output, MessageError& error);
    static bool IsAllowed(const ControlMessage& message, const ControlMessagePolicy& policy);
    // 不正RequestId・Policy対象外はHandlerを呼ばない。Handler例外は呼び出し側へ伝播する。
    static bool Dispatch(const ControlMessage& message, IControlMessageHandler& handler,
        MessageError& error, const ControlMessagePolicy& policy = {});
};
}
}
