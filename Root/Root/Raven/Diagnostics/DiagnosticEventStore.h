#pragma once

#include "Raven/Diagnostics/DiagnosticSink.h"

#include <memory>
#include <mutex>

namespace Raven::Diagnostics
{
struct DiagnosticStoreLimits
{
    std::size_t MaxEvents = 8192;
    std::size_t MaxRetainedBytes = 8u * 1024u * 1024u;
};

struct DiagnosticStoredEvent
{
    // Event.Sequenceとは独立したStore内通算ID。clearやring wrapでも再利用しません。
    std::uint64_t Id = 0;
    DiagnosticEvent Event;
};

struct DiagnosticStoreSnapshot
{
    std::uint64_t Revision = 0;
    std::uint64_t Evicted = 0;
    std::uint64_t Rejected = 0;
    std::size_t RetainedBytes = 0;
    std::size_t Capacity = 0;
    std::vector<DiagnosticStoredEvent> Events;
};

// WorkerはAppendだけ、UIは所有Snapshotだけを利用します。UI/Scene参照を保持しません。
class DiagnosticEventStore final
{
public:
    explicit DiagnosticEventStore(DiagnosticStoreLimits limits = {});
    void Append(const DiagnosticEvent& event);
    void Clear();
    // UIはWriterを待たず次の更新で再試行します。変更なし/競合時はnullptr。
    std::shared_ptr<const DiagnosticStoreSnapshot> TrySnapshot(std::uint64_t previousRevision) const;

private:
    void EvictOldest();
    DiagnosticStoreLimits m_Limits;
    mutable std::mutex m_Mutex;
    std::vector<DiagnosticStoredEvent> m_Ring;
    std::size_t m_Head = 0;
    std::size_t m_Count = 0;
    std::size_t m_RetainedBytes = 0;
    std::uint64_t m_NextId = 0;
    std::uint64_t m_Revision = 1;
    std::uint64_t m_Evicted = 0;
    std::uint64_t m_Rejected = 0;
};

class DiagnosticStoreSink final : public IDiagnosticSink
{
public:
    explicit DiagnosticStoreSink(std::shared_ptr<DiagnosticEventStore> store);
    bool Write(const DiagnosticEvent& event) override;
    bool Flush() override;
private:
    std::shared_ptr<DiagnosticEventStore> m_Store;
};

// 通常ApplicationのSessionとEditorが共有。UI破棄後もWorker側の所有権が残ります。
std::shared_ptr<DiagnosticEventStore> GetEditorDiagnosticEventStore();
}
