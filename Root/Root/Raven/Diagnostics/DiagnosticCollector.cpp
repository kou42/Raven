#include "Raven/Diagnostics/DiagnosticCollector.h"

#include <chrono>
#include <utility>

namespace Raven::Diagnostics
{
DiagnosticCollector::DiagnosticCollector(std::ostream& output)
    : m_Output(output)
{
}

DiagnosticCollector::~DiagnosticCollector()
{
    Stop();
}

bool DiagnosticCollector::RegisterBuffer(std::shared_ptr<EventBuffer> buffer)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Running == true || buffer == nullptr)
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
    return true;
}

bool DiagnosticCollector::Start()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Running == true || m_Worker.joinable() == true)
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
}

std::uint64_t DiagnosticCollector::GetCollectedCount() const noexcept
{
    return m_Collected.load(std::memory_order_relaxed);
}

bool DiagnosticCollector::DrainOnce()
{
    bool drained = false;
    for (const auto& buffer : m_Buffers)
    {
        DiagnosticEvent event;
        while (buffer->TryPop(event) == true)
        {
            WriteConsoleEvent(m_Output, event);
            m_Collected.fetch_add(1, std::memory_order_relaxed);
            drained = true;
        }
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
            // Stop前にPush済みのイベントを最後にDrainします。
            lock.unlock();
            DrainOnce();
            m_Output.flush();
            return;
        }
        if (drained == false)
        {
            // Notifyを取りこぼしても10ms以内に再走査します。
            m_Wake.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
}
}
