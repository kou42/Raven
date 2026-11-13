#pragma once

#include "Raven/Diagnostics/EventBuffer.h"
#include "Raven/Diagnostics/DiagnosticSink.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ostream>
#include <thread>
#include <vector>

namespace Raven::Diagnostics
{
// Bufferの所有権をCollectorとProducerで共有します。
// Registerは稼働中にも可能です。CollectorがBufferの所有権を保持します。
// 各BufferへのPushは単一Producerに限定します。
class DiagnosticCollector final
{
public:
    explicit DiagnosticCollector(std::ostream& output);
    explicit DiagnosticCollector(std::vector<std::unique_ptr<IDiagnosticSink>> sinks);
    ~DiagnosticCollector();

    DiagnosticCollector(const DiagnosticCollector&) = delete;
    DiagnosticCollector& operator=(const DiagnosticCollector&) = delete;

    bool RegisterBuffer(std::shared_ptr<EventBuffer> buffer);
    bool Start();
    void Notify();
    // Producerを停止した後に呼び、登録Bufferの残存イベントを排出します。
    void Flush();
    void Stop();
    std::uint64_t GetCollectedCount() const noexcept;
    std::uint64_t GetSinkFailureCount() const noexcept;
    std::size_t GetRegisteredBufferCount() const;

private:
    void Run();
    bool DrainOnce();
    void Dispatch(const DiagnosticEvent& event);
    void FlushSinks();
    void FailSink(std::size_t index) noexcept;

    // Sinkの参照先はWorker終了後まで保持し、失敗したSinkは再呼出ししません。
    std::vector<std::unique_ptr<IDiagnosticSink>> m_Sinks;
    std::vector<bool> m_FailedSinks;
    mutable std::mutex m_Mutex;
    std::condition_variable m_Wake;
    std::vector<std::shared_ptr<EventBuffer>> m_Buffers;
    std::thread m_Worker;
    bool m_Running = false;
    bool m_StopRequested = false;
    bool m_Stopped = false;
    std::uint64_t m_FlushRequested = 0;
    std::uint64_t m_FlushCompleted = 0;
    std::condition_variable m_FlushCondition;
    std::atomic<std::uint64_t> m_Collected{ 0 };
    std::atomic<std::uint64_t> m_SinkFailures{ 0 };
};
}
