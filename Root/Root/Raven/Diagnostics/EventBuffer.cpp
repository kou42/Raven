#include "Raven/Diagnostics/EventBuffer.h"

#include <utility>

namespace Raven::Diagnostics
{
bool EventBuffer::TryPush(DiagnosticEvent event)
{
    const std::uint64_t write = m_WriteIndex.load(std::memory_order_relaxed);
    const std::uint64_t read = m_ReadIndex.load(std::memory_order_acquire);
    if (write - read >= Capacity)
    {
        m_DroppedCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Payloadを含む全書き込みをreleaseで公開し、Consumerのacquireと同期します。
    m_Events[write % Capacity] = std::move(event);
    m_WriteIndex.store(write + 1, std::memory_order_release);
    return true;
}

bool EventBuffer::TryPop(DiagnosticEvent& event)
{
    const std::uint64_t read = m_ReadIndex.load(std::memory_order_relaxed);
    const std::uint64_t write = m_WriteIndex.load(std::memory_order_acquire);
    if (read == write)
    {
        return false;
    }

    event = std::move(m_Events[read % Capacity]);
    // Consumerが読み終えたslotのみProducerに再利用を許可します。
    m_ReadIndex.store(read + 1, std::memory_order_release);
    return true;
}

std::uint64_t EventBuffer::GetDroppedCount() const noexcept
{
    return m_DroppedCount.load(std::memory_order_relaxed);
}
}
