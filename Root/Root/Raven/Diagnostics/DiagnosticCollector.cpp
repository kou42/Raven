#include "Raven/Diagnostics/DiagnosticCollector.h"

#include <chrono>
#include <algorithm>
#include <utility>

namespace Raven::Diagnostics
{
DiagnosticCollector::DiagnosticCollector(std::ostream& output)
{
    m_Sinks.push_back(std::make_unique<ConsoleSink>(output));
    m_FailedSinks.resize(m_Sinks.size(), false);
}

DiagnosticCollector::DiagnosticCollector(std::vector<std::unique_ptr<IDiagnosticSink>> sinks)
    : m_Sinks(std::move(sinks)), m_FailedSinks(m_Sinks.size(), false)
{
}

DiagnosticCollector::~DiagnosticCollector()
{
    Stop();
}

bool DiagnosticCollector::RegisterBuffer(std::shared_ptr<EventBuffer> buffer)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_StopRequested == true || m_Stopped == true || buffer == nullptr)
    {
        return false;
    }
    for (const auto& registered : m_Buffers)
    {
        if (registered == buffer)
        {
            return false;
        }
    }
    m_Buffers.push_back(std::move(buffer));
    m_Wake.notify_one();
    return true;
}

bool DiagnosticCollector::Start()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Running == true || m_Stopped == true || m_Worker.joinable() == true)
    {
        return false;
    }
    m_StopRequested = false;
    m_Running = true;
    try
    {
        m_Worker = std::thread(&DiagnosticCollector::Run, this);
    }
    catch (...)
    {
        m_Running = false;
        throw;
    }
    return true;
}

void DiagnosticCollector::Notify()
{
    m_Wake.notify_one();
}

void DiagnosticCollector::Flush()
{
    std::unique_lock<std::mutex> lock(m_Mutex);
    if (m_Running == false || m_StopRequested == true)
    {
        return;
    }
    const std::uint64_t request = ++m_FlushRequested;
    m_Wake.notify_one();
    m_FlushCondition.wait(lock, [this, request]()
    {
        return m_FlushCompleted >= request || m_Running == false;
    });
}

void DiagnosticCollector::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Running == false)
        {
            return;
        }
        m_StopRequested = true;
    }
    m_Wake.notify_one();
    if (m_Worker.joinable() == true)
    {
        m_Worker.join();
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Running = false;
    m_Stopped = true;
    m_FlushCondition.notify_all();
}

std::uint64_t DiagnosticCollector::GetCollectedCount() const noexcept
{
    return m_Collected.load(std::memory_order_relaxed);
}

std::uint64_t DiagnosticCollector::GetSinkFailureCount() const noexcept
{
    return m_SinkFailures.load(std::memory_order_relaxed);
}

std::size_t DiagnosticCollector::GetRegisteredBufferCount() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Buffers.size();
}

void DiagnosticCollector::FailSink(std::size_t index) noexcept
{
    m_FailedSinks[index] = true;
    m_SinkFailures.fetch_add(1, std::memory_order_relaxed);
    // 通常のEmitへ戻すと同じ故障Sinkへ再帰するため、独立した最終出力先を使います。
    WriteEmergencyDiagnostic("Diagnostics sink failed; remaining sinks continue.");
}

void DiagnosticCollector::Dispatch(const DiagnosticEvent& event)
{
    for (std::size_t index = 0; index < m_Sinks.size(); ++index)
    {
        if (m_FailedSinks[index] == true)
        {
            continue;
        }
        try
        {
            if (m_Sinks[index] == nullptr || m_Sinks[index]->Write(event) == false)
            {
                FailSink(index);
            }
        }
        catch (...)
        {
            FailSink(index);
        }
    }
}

void DiagnosticCollector::FlushSinks()
{
    for (std::size_t index = 0; index < m_Sinks.size(); ++index)
    {
        if (m_FailedSinks[index] == true)
        {
            continue;
        }
        try
        {
            if (m_Sinks[index] == nullptr || m_Sinks[index]->Flush() == false)
            {
                FailSink(index);
            }
        }
        catch (...)
        {
            FailSink(index);
        }
    }
}

bool DiagnosticCollector::DrainOnce()
{
    bool drained = false;
    // 登録は実行中にも発生するため、短時間のロックで共有所有権をSnapshotします。
    std::vector<std::shared_ptr<EventBuffer>> buffers;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        buffers = m_Buffers;
    }
    for (const auto& buffer : buffers)
    {
        DiagnosticEvent event;
        // 一度の走査で無制限に消費すると、高頻度ProducerによりStop/Flushが飢餓します。
        // Bufferごとの処理量を制限して制御要求の確認機会を確保します。
        for (std::size_t index = 0; index < EventBuffer::Capacity; ++index)
        {
            if (buffer->TryPop(event) == false)
            {
                break;
            }
            Dispatch(event);
            m_Collected.fetch_add(1, std::memory_order_relaxed);
            drained = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        // 終了したThreadのBufferは残存イベントをDrainしてから登録を解除します。
        // SnapshotとTLSが共有所有するため、解除時にProducer参照を無効化しません。
        std::erase_if(m_Buffers, [](const auto& buffer)
        {
            return buffer->IsClosedAndEmpty();
        });
    }
    return drained;
}

void DiagnosticCollector::Run()
{
    for (;;)
    {
        const bool drained = DrainOnce();
        std::unique_lock<std::mutex> lock(m_Mutex);
        if (m_StopRequested == true)
        {
            lock.unlock();
            // Stop呼出側は全Producerをjoin済みであることが前提です。
            DrainOnce();
            FlushSinks();
            return;
        }
        if (m_FlushRequested > m_FlushCompleted)
        {
            const std::uint64_t request = m_FlushRequested;
            lock.unlock();
            // Flush時点で既にPush済みのイベントを出力します。
            DrainOnce();
            FlushSinks();
            lock.lock();
            m_FlushCompleted = request;
            m_FlushCondition.notify_all();
        }
        if (drained == false)
        {
            m_Wake.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
}
}
