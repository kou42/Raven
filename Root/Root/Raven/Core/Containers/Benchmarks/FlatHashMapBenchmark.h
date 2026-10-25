#pragma once

#include "Raven/Core/Containers/FlatHashMap.h"
#include "Raven/Core/Memory/FrameAllocator.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Raven::benchmarks
{
namespace detail
{
struct StandardAllocationStatistics
{
    std::size_t CurrentBytes = 0;
    std::size_t PeakBytes = 0;
    std::size_t AllocationCount = 0;
};

// std::unordered_mapのnodeとbucket確保を同じ統計へ集約します。
// allocator自身やCRT内部の管理領域は含まないため、Raven側の一時std::vectorと同様に
// Container外部の補助allocationは比較対象外です。
template<typename T>
class StandardCountingAllocator
{
    template<typename>
    friend class StandardCountingAllocator;

public:
    using value_type = T;

    StandardCountingAllocator() noexcept = default;
    explicit StandardCountingAllocator(StandardAllocationStatistics& statistics) noexcept
        : m_Statistics(&statistics) {}

    template<typename U>
    StandardCountingAllocator(const StandardCountingAllocator<U>& other) noexcept
        : m_Statistics(other.m_Statistics) {}

    [[nodiscard]] T* allocate(std::size_t count)
    {
        T* memory = std::allocator<T>{}.allocate(count);
        if (m_Statistics != nullptr)
        {
            const std::size_t bytes = count * sizeof(T);
            m_Statistics->CurrentBytes += bytes;
            m_Statistics->PeakBytes = (std::max)(
                m_Statistics->PeakBytes,
                m_Statistics->CurrentBytes);
            ++m_Statistics->AllocationCount;
        }
        return memory;
    }

    void deallocate(T* memory, std::size_t count) noexcept
    {
        if (m_Statistics != nullptr)
        {
            m_Statistics->CurrentBytes -= count * sizeof(T);
        }
        std::allocator<T>{}.deallocate(memory, count);
    }

    template<typename U>
    bool operator==(const StandardCountingAllocator<U>& other) const noexcept
    {
        return m_Statistics == other.m_Statistics;
    }

    template<typename U>
    bool operator!=(const StandardCountingAllocator<U>& other) const noexcept
    {
        return (*this == other) == false;
    }

private:
    StandardAllocationStatistics* m_Statistics = nullptr;
};
}

// GPU不要のCPU計測です。入力生成・Allocator構築・破棄を計測から除外します。
// reserveあり／なし／erase後の再挿入を同じKey/Value列で比較します。
inline int RunFlatHashMapBenchmark()
{
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t Warmups = 3;
    constexpr std::size_t Samples = 9;
    constexpr std::size_t Counts[] = { 128, 1024, 8192, 65536 };
    volatile std::uint64_t checksum = 0;

    for (const std::size_t count : Counts)
    {
        std::vector<std::uint64_t> keys;
        keys.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            keys.push_back(static_cast<std::uint64_t>(i * 2654435761ULL));
        }

        for (int scenario = 0; scenario < 3; ++scenario)
        {
            std::vector<double> ravenTimes;
            std::vector<double> standardTimes;
            std::size_t ravenAllocations = 0;
            std::size_t ravenPeak = 0;
            std::size_t ravenCapacity = 0;
            std::size_t standardAllocations = 0;
            std::size_t standardPeak = 0;
            std::size_t standardBuckets = 0;

            for (std::size_t sample = 0; sample < Warmups + Samples; ++sample)
            {
                FrameAllocator allocator(count * 512 + 65536);
                FlatHashMap<std::uint64_t, std::uint64_t> raven(allocator);
                const auto ravenStart = Clock::now();
                if (scenario != 1) { raven.reserve(count); }
                for (std::size_t i = 0; i < count; ++i)
                {
                    raven.emplace(keys[i], static_cast<std::uint64_t>(i));
                }
                if (scenario == 2)
                {
                    for (std::size_t i = 0; i < count; i += 2) { raven.erase(keys[i]); }
                    for (std::size_t i = 0; i < count; i += 2)
                    {
                        raven.emplace(keys[i] + 1, static_cast<std::uint64_t>(i));
                    }
                }
                for (const auto key : keys)
                {
                    const auto* value = raven.find(key);
                    checksum = checksum + (value == nullptr ? 0 : *value);
                }
                const auto ravenEnd = Clock::now();
                ravenAllocations = allocator.GetAllocationCount();
                ravenPeak = allocator.GetPeakUsedMemory();
                ravenCapacity = raven.capacity();

                detail::StandardAllocationStatistics standardStatistics;
                using StandardValue = std::pair<const std::uint64_t, std::uint64_t>;
                using StandardAllocator = detail::StandardCountingAllocator<StandardValue>;
                using StandardMap = std::unordered_map<
                    std::uint64_t,
                    std::uint64_t,
                    std::hash<std::uint64_t>,
                    std::equal_to<std::uint64_t>,
                    StandardAllocator>;
                StandardMap standard(
                    0,
                    std::hash<std::uint64_t>{},
                    std::equal_to<std::uint64_t>{},
                    StandardAllocator(standardStatistics));
                const auto standardStart = Clock::now();
                if (scenario != 1) { standard.reserve(count); }
                for (std::size_t i = 0; i < count; ++i)
                {
                    standard.emplace(keys[i], static_cast<std::uint64_t>(i));
                }
                if (scenario == 2)
                {
                    for (std::size_t i = 0; i < count; i += 2) { standard.erase(keys[i]); }
                    for (std::size_t i = 0; i < count; i += 2)
                    {
                        standard.emplace(keys[i] + 1, static_cast<std::uint64_t>(i));
                    }
                }
                for (const auto key : keys)
                {
                    const auto found = standard.find(key);
                    checksum = checksum + (found == standard.end() ? 0 : found->second);
                }
                const auto standardEnd = Clock::now();
                standardAllocations = standardStatistics.AllocationCount;
                standardPeak = standardStatistics.PeakBytes;
                standardBuckets = standard.bucket_count();

                if (sample >= Warmups)
                {
                    ravenTimes.push_back(std::chrono::duration<double, std::micro>(ravenEnd - ravenStart).count());
                    standardTimes.push_back(std::chrono::duration<double, std::micro>(standardEnd - standardStart).count());
                }
            }

            std::sort(ravenTimes.begin(), ravenTimes.end());
            std::sort(standardTimes.begin(), standardTimes.end());
            const double ravenMedian = ravenTimes[Samples / 2];
            const double standardMedian = standardTimes[Samples / 2];
            std::cout << "[FlatHashMap Benchmark] count=" << count
                << " scenario=" << (scenario == 0 ? "reserved" : scenario == 1 ? "growth" : "erase-insert")
                << " Raven_us=" << ravenMedian
                << " std_us=" << standardMedian
                << " Raven_over_std=" << (standardMedian == 0.0 ? 0.0 : ravenMedian / standardMedian)
                << " Raven_allocations=" << ravenAllocations
                << " Raven_peak_bytes=" << ravenPeak
                << " Raven_capacity=" << ravenCapacity
                << " std_allocations=" << standardAllocations
                << " std_peak_bytes=" << standardPeak
                << " std_buckets=" << standardBuckets << '\n';
        }
    }
    std::cout << "[FlatHashMap Benchmark] checksum=" << checksum << '\n';
    return 0;
}
} // namespace Raven::benchmarks
