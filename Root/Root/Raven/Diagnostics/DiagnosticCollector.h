#pragma once

#include "Raven/Diagnostics/EventBuffer.h"

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
// RegisterはStart前に呼び、Stop完了まで登録Bufferを生存させてください。
// 各BufferへのPushは単一Producerに限定します。
class DiagnosticCollector final
{
public:
    explicit DiagnosticCollector(std::ostream& output);
    ~DiagnosticCollector();

    DiagnosticCollector(const DiagnosticCollector&) = delete;
    DiagnosticCollector& operator=(const DiagnosticCollector&) = delete;

    bool RegisterBuffer(std::shared_ptr<EventBuffer> buffer);
    bool Start();
    void Notify();
    void Stop();
    std::uint64_t GetCollectedCount() const noexcept;

private:
    void Run();
    bool DrainOnce();

    std::ostream& m_Output;
    std::mutex m_Mutex;
    std::condition_variable m_Wake;
    std::vector<std::shared_ptr<EventBuffer>> m_Buffers;
    std::thread m_Worker;
    bool m_Running = false;
    bool m_StopRequested = false;
    std::atomic<std::uint64_t> m_Collected{ 0 };
};
}
