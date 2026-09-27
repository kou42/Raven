#include "Raven/Core/Jobs/Tests/JobSystemSelfTests.h"

#include "Raven/Core/Jobs/JobSystem.h"

#include <atomic>
#include <cassert>
#include <future>
#include <vector>

namespace Raven::tests
{

void RunJobSystemSelfTests()
{
    JobSystem jobs(2u);
    assert(jobs.GetWorkerCount() == 2u);
    assert(jobs.IsStopping() == false);

    std::future<int> value = jobs.Submit([]()
    {
        return 42;
    });
    assert(value.valid() == true);
    assert(value.get() == 42);

    std::atomic<int> completed{ 0 };
    std::vector<std::future<void>> futures;
    futures.reserve(16u);
    for (int index = 0; index < 16; ++index)
    {
        futures.emplace_back(jobs.Submit([&completed]()
        {
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (std::future<void>& future : futures)
    {
        assert(future.valid() == true);
        future.get();
    }
    assert(completed.load(std::memory_order_relaxed) == 16);
}

} // namespace Raven::tests
