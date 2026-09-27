#include "Raven/Core/Jobs/JobSystem.h"

#include <algorithm>

namespace Raven
{

JobSystem::JobSystem(std::size_t workerCount)
{
    if (workerCount == 0u)
    {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        // Main/Application Threadを1本残しつつ、hardware_concurrency不明時も最低1 Workerを確保します。
        workerCount = hardwareThreads > 1u
            ? static_cast<std::size_t>(hardwareThreads - 1u)
            : 1u;
    }

    m_Workers.reserve(workerCount);
    for (std::size_t workerIndex = 0u; workerIndex < workerCount; ++workerIndex)
    {
        m_Workers.emplace_back([this]()
        {
            WorkerLoop();
        });
    }
}

JobSystem::~JobSystem()
{
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Stopping = true;
    }
    m_Condition.notify_all();

    for (std::thread& worker : m_Workers)
    {
        if (worker.joinable() == true)
        {
            worker.join();
        }
    }
}

void JobSystem::WorkerLoop()
{
    for (;;)
    {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Condition.wait(lock, [this]()
            {
                return m_Stopping == true || m_Jobs.empty() == false;
            });

            if (m_Stopping == true && m_Jobs.empty() == true)
            {
                return;
            }

            job = std::move(m_Jobs.front());
            m_Jobs.pop();
        }

        job();
    }
}

} // namespace Raven
