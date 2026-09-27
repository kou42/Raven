#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Raven
{

// Engine全体から再利用する最小Worker Poolです。
// JobはCPU側処理だけを対象とし、Renderer / Window / Scene等のMain Thread専用Objectへ触れません。
class JobSystem
{
public:
    explicit JobSystem(std::size_t workerCount = 0u);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    template<typename Function>
    auto Submit(Function&& function) -> std::future<std::invoke_result_t<Function>>
    {
        using Result = std::invoke_result_t<Function>;

        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
        std::future<Result> future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Stopping == true)
            {
                return {};
            }

            m_Jobs.emplace([task]()
            {
                (*task)();
            });
        }

        m_Condition.notify_one();
        return future;
    }

    std::size_t GetWorkerCount() const { return m_Workers.size(); }
    bool IsStopping() const;

private:
    void WorkerLoop();

private:
    std::vector<std::thread> m_Workers;
    std::queue<std::function<void()>> m_Jobs;
    std::mutex m_Mutex;
    std::condition_variable m_Condition;
    bool m_Stopping = false;
};

} // namespace Raven
