#include "Raven/Diagnostics/DiagnosticEventStore.h"

#include <algorithm>

namespace Raven::Diagnostics
{
namespace
{
std::size_t StringBytes(const DiagnosticEvent& event)
{
    if (const auto* text = std::get_if<std::string>(&event.Payload); text != nullptr)
    {
        return text->capacity() + 1;
    }
    if (const auto* validation = std::get_if<DiagnosticValidation>(&event.Payload); validation != nullptr)
    {
        return validation->Name.capacity() + validation->Message.capacity() + 2;
    }
    if (const auto* message = std::get_if<DiagnosticMessage>(&event.Payload); message != nullptr)
    {
        std::size_t bytes = message->Arguments.capacity() * sizeof(DiagnosticArgument);
        for (const auto& argument : message->Arguments)
        {
            if (const auto* text = std::get_if<std::string>(&argument); text != nullptr) { bytes += text->capacity() + 1; }
        }
        return bytes;
    }
    return 0;
}

bool Valid(const DiagnosticEvent& event)
{
    if (event.Id == 0 || DiagnosticFilter{}.Accepts(event) == false) { return false; }
    if (const auto* text = std::get_if<std::string>(&event.Payload); text != nullptr)
    {
        return text->size() <= MaxDiagnosticTextBytes;
    }
    if (const auto* metric = std::get_if<DiagnosticMetric>(&event.Payload); metric != nullptr) { return metric->IsValid(); }
    if (const auto* validation = std::get_if<DiagnosticValidation>(&event.Payload); validation != nullptr) { return validation->IsValid(); }
    if (const auto* message = std::get_if<DiagnosticMessage>(&event.Payload); message != nullptr) { return message->IsValid(); }
    return true;
}
}

DiagnosticEventStore::DiagnosticEventStore(DiagnosticStoreLimits limits)
    : m_Limits(limits)
{
    // 設定ミスでも無制限確保を行いません。固定slot領域もbyte予算へ含めます。
    m_Limits.MaxRetainedBytes = std::clamp<std::size_t>(limits.MaxRetainedBytes, sizeof(DiagnosticStoredEvent), 64u * 1024u * 1024u);
    const auto capacity = std::min<std::size_t>(std::clamp<std::size_t>(limits.MaxEvents, 1, 65536),
        m_Limits.MaxRetainedBytes / sizeof(DiagnosticStoredEvent));
    m_Ring.resize(capacity);
    m_RetainedBytes = m_Ring.capacity() * sizeof(DiagnosticStoredEvent);
}

void DiagnosticEventStore::EvictOldest()
{
    m_RetainedBytes -= StringBytes(m_Ring[m_Head].Event);
    m_Ring[m_Head] = DiagnosticStoredEvent{};
    m_Head = (m_Head + 1) % m_Ring.size();
    --m_Count;
    ++m_Evicted;
}

void DiagnosticEventStore::Append(const DiagnosticEvent& event)
{
    // 巨大な外部Payloadをコピーする前に、既存の型別上限を検証します。
    if (Valid(event) == false)
    {
        std::lock_guard lock(m_Mutex);
        ++m_Rejected;
        ++m_Revision;
        return;
    }
    DiagnosticStoredEvent copy;
    copy.Event = event;
    const auto extra = StringBytes(copy.Event);
    std::lock_guard lock(m_Mutex);
    ++m_Revision;
    if (m_Ring.capacity() * sizeof(DiagnosticStoredEvent) + extra > m_Limits.MaxRetainedBytes)
    {
        ++m_Rejected;
        return;
    }
    while (m_Count > 0 && (m_Count == m_Ring.size() || m_RetainedBytes + extra > m_Limits.MaxRetainedBytes))
    {
        EvictOldest();
    }
    copy.Id = ++m_NextId;
    m_Ring[(m_Head + m_Count) % m_Ring.size()] = std::move(copy);
    ++m_Count;
    m_RetainedBytes += extra;
}

void DiagnosticEventStore::Clear()
{
    std::lock_guard lock(m_Mutex);
    for (auto& event : m_Ring) { event = DiagnosticStoredEvent{}; }
    m_Count = 0;
    m_Head = 0;
    m_RetainedBytes = m_Ring.capacity() * sizeof(DiagnosticStoredEvent);
    m_Evicted = 0;
    m_Rejected = 0;
    ++m_Revision;
}

std::shared_ptr<const DiagnosticStoreSnapshot> DiagnosticEventStore::TrySnapshot(std::uint64_t previousRevision) const
{
    std::unique_lock lock(m_Mutex, std::try_to_lock);
    if (lock.owns_lock() == false || previousRevision == m_Revision) { return nullptr; }
    auto result = std::make_shared<DiagnosticStoreSnapshot>();
    result->Revision = m_Revision;
    result->Evicted = m_Evicted;
    result->Rejected = m_Rejected;
    result->RetainedBytes = m_RetainedBytes;
    result->Capacity = m_Ring.size();
    result->Events.reserve(m_Count);
    for (std::size_t index = 0; index < m_Count; ++index)
    {
        result->Events.push_back(m_Ring[(m_Head + index) % m_Ring.size()]);
    }
    return result;
}

DiagnosticStoreSink::DiagnosticStoreSink(std::shared_ptr<DiagnosticEventStore> store) : m_Store(std::move(store)) {}
bool DiagnosticStoreSink::Write(const DiagnosticEvent& event)
{
    if (m_Store == nullptr) { return false; }
    m_Store->Append(event);
    return true;
}
bool DiagnosticStoreSink::Flush() { return true; }

std::shared_ptr<DiagnosticEventStore> GetEditorDiagnosticEventStore()
{
    static auto store = std::make_shared<DiagnosticEventStore>();
    return store;
}
}
