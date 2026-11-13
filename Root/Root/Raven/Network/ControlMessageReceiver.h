#pragma once

#include "Raven/Network/ControlMessageCodec.h"
#include <array>
#include <cstddef>
#include <deque>
#include <mutex>

namespace Raven
{
namespace Network
{
enum class MessageTransport { Stream, Datagram };
enum class ReceiverState { Open, Closed, Failed };
struct MessageReceiveLimits
{
    std::size_t MaxQueuedMessages = 64;
    // Prefixを除く受信時のMessagePack長の合計。Queueの実メモリ量そのものではない。
    std::size_t MaxQueuedBytes = 64u * 1024u;
};

// 1接続/Session用。WorkerでReceive、Main ThreadでTryPopが可能。
// 全APIをmutexで直列化し、CallbackやScene変更はLock内で実行しない。
// 再接続は新しいInstanceを生成する。破棄前に呼び出し元のThreadを停止/joinすること。
class ControlMessageReceiver
{
public:
    explicit ControlMessageReceiver(MessageTransport transport,
        const MessageReceiveLimits& limits = {}, const ControlMessagePolicy& policy = {});
    ControlMessageReceiver(const ControlMessageReceiver&) = delete;
    ControlMessageReceiver& operator=(const ControlMessageReceiver&) = delete;

    // chunk全体を保存せず、Prefix検査後に最大1 Frame分だけ所有する。
    // 途中に不正Frame/容量超過があればSessionを失敗状態にし、それ以前の未処理Queueも破棄する。
    bool ReceiveStream(std::string_view chunk);
    bool ReceiveDatagram(std::string_view datagram);
    // EOFを通知する。未完Prefix/BodyがあればTruncated。完了済みQueueも切断時に破棄する。
    bool FinishStream();
    void Disconnect();
    // 空/切断/失敗時はfalseでoutputを保持。取り出した値は呼び出し側へ所有権が移る。
    // Disconnectは既に取り出した値を取り消さない。Sessionとの対応は利用側で管理する。
    bool TryPop(ControlMessage& output);
    ReceiverState GetState() const;
    MessageError GetError() const;
    std::size_t GetQueuedCount() const;
    std::size_t GetQueuedBytes() const;
private:
    struct QueuedMessage
    {
        ControlMessage Value;
        std::size_t WireBytes = 0;
    };
    bool EnqueueLocked(std::string_view bytes);
    bool FailLocked(const MessageError& error);
    void ClearLocked();

    const MessageTransport m_Transport;
    const MessageReceiveLimits m_Limits;
    const ControlMessagePolicy m_Policy;
    mutable std::mutex m_Mutex;
    ReceiverState m_State = ReceiverState::Open;
    MessageError m_Error;
    std::array<unsigned char, 4> m_Header{};
    std::size_t m_HeaderSize = 0;
    std::uint32_t m_ExpectedBytes = 0;
    std::string m_Pending;
    std::deque<QueuedMessage> m_Queue;
    std::size_t m_QueuedBytes = 0;
};
}
}
