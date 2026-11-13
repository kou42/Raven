#include "Raven/Network/ControlMessageReceiver.h"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace Raven
{
namespace Network
{
ControlMessageReceiver::ControlMessageReceiver(MessageTransport transport,
    const MessageReceiveLimits& limits, const ControlMessagePolicy& policy)
    : m_Transport(transport), m_Limits(limits), m_Policy(policy)
{
}

void ControlMessageReceiver::ClearLocked()
{
    m_Queue.clear();
    m_QueuedBytes = 0;
    m_Pending.clear();
    m_HeaderSize = 0;
    m_ExpectedBytes = 0;
}

bool ControlMessageReceiver::FailLocked(const MessageError& error)
{
    ClearLocked();
    m_State = ReceiverState::Failed;
    m_Error = error;
    return false;
}

bool ControlMessageReceiver::EnqueueLocked(std::string_view bytes)
{
    // 減算で検査し、設定値や入力長による加算Overflowを避ける。
    if (m_Queue.size() >= m_Limits.MaxQueuedMessages
        || bytes.size() > m_Limits.MaxQueuedBytes - m_QueuedBytes)
    {
        return FailLocked({MessageErrorCode::QueueFull});
    }
    ControlMessage value;
    MessageError error;
    if (ControlMessageCodec::Decode(bytes, value, error) == false)
    {
        return FailLocked(error);
    }
    if (ControlMessageCodec::IsAllowed(value, m_Policy) == false)
    {
        return FailLocked({MessageErrorCode::UnsupportedMessage});
    }
    m_Queue.push_back({value, bytes.size()});
    m_QueuedBytes += bytes.size();
    return true;
}

bool ControlMessageReceiver::ReceiveStream(std::string_view chunk)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != ReceiverState::Open)
    {
        return false;
    }
    if (m_Transport != MessageTransport::Stream)
    {
        return FailLocked({MessageErrorCode::WrongTransport});
    }
    try
    {
        while (chunk.empty() == false)
        {
            if (m_HeaderSize < m_Header.size())
            {
                const auto count = (std::min)(chunk.size(), m_Header.size() - m_HeaderSize);
                for (std::size_t i = 0; i < count; ++i)
                {
                    m_Header[m_HeaderSize++] = static_cast<unsigned char>(chunk[i]);
                }
                chunk.remove_prefix(count);
                if (m_HeaderSize < m_Header.size())
                {
                    continue;
                }
                m_ExpectedBytes = 0;
                for (const auto byte : m_Header)
                {
                    m_ExpectedBytes = (m_ExpectedBytes << 8u) | byte;
                }
                // 巨大/ゼロ長PrefixをBody確保前に拒否。Frame数分の一括確保は行わない。
                if (m_ExpectedBytes == 0 || m_ExpectedBytes > MaxControlMessageBytes)
                {
                    return FailLocked({MessageErrorCode::SizeLimit});
                }
            }
            const auto count = (std::min)(chunk.size(), m_ExpectedBytes - m_Pending.size());
            m_Pending.append(chunk.data(), count);
            chunk.remove_prefix(count);
            if (m_Pending.size() == m_ExpectedBytes)
            {
                // Decode中だけViewを使う。QueueはDocumentもPending Bufferも参照しない。
                if (EnqueueLocked(m_Pending) == false)
                {
                    return false;
                }
                m_Pending.clear();
                m_HeaderSize = 0;
                m_ExpectedBytes = 0;
            }
        }
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return FailLocked({MessageErrorCode::OutOfMemory});
    }
    catch (const std::length_error&)
    {
        return FailLocked({MessageErrorCode::SizeLimit});
    }
}

bool ControlMessageReceiver::ReceiveDatagram(std::string_view datagram)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != ReceiverState::Open)
    {
        return false;
    }
    if (m_Transport != MessageTransport::Datagram)
    {
        return FailLocked({MessageErrorCode::WrongTransport});
    }
    if (datagram.empty() || datagram.size() > MaxControlDatagramBytes)
    {
        return FailLocked({MessageErrorCode::SizeLimit});
    }
    try
    {
        return EnqueueLocked(datagram);
    }
    catch (const std::bad_alloc&)
    {
        return FailLocked({MessageErrorCode::OutOfMemory});
    }
    catch (const std::length_error&)
    {
        return FailLocked({MessageErrorCode::SizeLimit});
    }
}

bool ControlMessageReceiver::FinishStream()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != ReceiverState::Open)
    {
        return false;
    }
    if (m_Transport != MessageTransport::Stream)
    {
        return FailLocked({MessageErrorCode::WrongTransport});
    }
    if (m_HeaderSize != 0)
    {
        return FailLocked({MessageErrorCode::Truncated});
    }
    ClearLocked();
    m_State = ReceiverState::Closed;
    m_Error = {MessageErrorCode::Closed};
    return true;
}

void ControlMessageReceiver::Disconnect()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    ClearLocked();
    // 失敗原因は切断通知で上書きしない。
    if (m_State == ReceiverState::Open)
    {
        m_State = ReceiverState::Closed;
        m_Error = {MessageErrorCode::Closed};
    }
}

bool ControlMessageReceiver::TryPop(ControlMessage& output)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_State != ReceiverState::Open || m_Queue.empty())
    {
        return false;
    }
    output = m_Queue.front().Value;
    m_QueuedBytes -= m_Queue.front().WireBytes;
    m_Queue.pop_front();
    return true;
}

ReceiverState ControlMessageReceiver::GetState() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_State;
}
MessageError ControlMessageReceiver::GetError() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Error;
}
std::size_t ControlMessageReceiver::GetQueuedCount() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Queue.size();
}
std::size_t ControlMessageReceiver::GetQueuedBytes() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_QueuedBytes;
}
}
}
