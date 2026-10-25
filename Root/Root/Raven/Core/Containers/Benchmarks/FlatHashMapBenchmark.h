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

struct GroupedHasher
{
    std::size_t operator()(std::uint64_t key) const noexcept
    {
        // 8個の連続Keyを同じHashへ集約しつつ理想Bucket間に空きを残し、
        // 全要素が1本の巨大Clusterになる非現実的なO(N^2)計測を避けます。
        return static_cast<std::size_t>((key / 8) * 16);
    }
};

struct HashCallStatistics
{
    std::size_t Calls = 0;
};

struct CountingHasher
{
    HashCallStatistics* Statistics = nullptr;

    std::size_t operator()(std::uint64_t key) const noexcept
    {
        if (Statistics != nullptr)
        {
            ++Statistics->Calls;
        }
        return std::hash<std::uint64_t>{}(key);
    }
};

class TimedFrameAllocator final : public Allocator
{
public:
    explicit TimedFrameAllocator(std::size_t capacity) : m_Allocator(capacity) {}

    void* Allocate(std::size_t size, std::size_t alignment) override
    {
        const auto start = std::chrono::steady_clock::now();
        void* memory = m_Allocator.Allocate(size, alignment);
        const auto end = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double, std::micro>(end - start).count();
        m_TotalAllocationMicroseconds += elapsed;
        return memory;
    }

    void Deallocate(void* memory) override
    {
        const auto start = std::chrono::steady_clock::now();
        m_Allocator.Deallocate(memory);
        const auto end = std::chrono::steady_clock::now();
        m_TotalDeallocationMicroseconds +=
            std::chrono::duration<double, std::micro>(end - start).count();
    }
    void Reset() override { m_Allocator.Reset(); }
    std::size_t GetCapacity() const override { return m_Allocator.GetCapacity(); }
    std::size_t GetUsedMemory() const override { return m_Allocator.GetUsedMemory(); }
    std::size_t GetPeakUsedMemory() const override { return m_Allocator.GetPeakUsedMemory(); }
    std::size_t GetAllocationCount() const override { return m_Allocator.GetAllocationCount(); }
    double GetTotalAllocationMicroseconds() const noexcept { return m_TotalAllocationMicroseconds; }
    double GetTotalDeallocationMicroseconds() const noexcept { return m_TotalDeallocationMicroseconds; }

private:
    FrameAllocator m_Allocator;
    double m_TotalAllocationMicroseconds = 0.0;
    double m_TotalDeallocationMicroseconds = 0.0;
};

enum class DetailedScenario
{
    InsertOnly,
    FindHit,
    FindMiss,
    Erase,
    RehashOnly,
    CollisionHeavy,
    TombstoneHeavy
};

inline const char* GetDetailedScenarioName(DetailedScenario scenario)
{
    switch (scenario)
    {
    case DetailedScenario::InsertOnly: return "insert-only";
    case DetailedScenario::FindHit: return "find-hit";
    case DetailedScenario::FindMiss: return "find-miss";
    case DetailedScenario::Erase: return "erase";
    case DetailedScenario::RehashOnly: return "rehash-only";
    case DetailedScenario::CollisionHeavy: return "collision-heavy";
    case DetailedScenario::TombstoneHeavy: return "tombstone-heavy";
    }
    return "unknown";
}

inline double Median(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

struct DetailedRunResult
{
    double Microseconds = 0.0;
    std::size_t Capacity = 0;
    std::size_t Tombstones = 0;
    double AverageProbe = 0.0;
    std::size_t MaximumProbe = 0;
    double CollisionRate = 0.0;
};

template<typename Hasher>
DetailedRunResult RunRavenDetailedScenario(
    DetailedScenario scenario,
    const std::vector<std::uint64_t>& keys,
    const std::vector<std::uint64_t>& missKeys,
    std::uint64_t& checksum)
{
    using Clock = std::chrono::steady_clock;
    const std::size_t count = keys.size();
    FrameAllocator allocator(count * 1024 + 65536);
    FlatHashMap<std::uint64_t, std::uint64_t, Hasher> map(allocator);

    if (scenario != DetailedScenario::InsertOnly &&
        scenario != DetailedScenario::CollisionHeavy)
    {
        map.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            map.emplace(keys[index], static_cast<std::uint64_t>(index));
        }
    }
    else
    {
        map.reserve(count);
    }

    if (scenario == DetailedScenario::TombstoneHeavy)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            if ((index % 4) != 0)
            {
                map.erase(keys[index]);
            }
        }
    }

    const auto start = Clock::now();
    switch (scenario)
    {
    case DetailedScenario::InsertOnly:
        for (std::size_t index = 0; index < count; ++index)
        {
            map.emplace(keys[index], static_cast<std::uint64_t>(index));
        }
        break;
    case DetailedScenario::FindHit:
        for (std::size_t repeat = 0; repeat < 8; ++repeat)
        {
            for (const std::uint64_t key : keys)
            {
                const std::uint64_t* value = map.find(key);
                checksum += value == nullptr ? 0 : *value;
            }
        }
        break;
    case DetailedScenario::FindMiss:
        for (std::size_t repeat = 0; repeat < 8; ++repeat)
        {
            for (const std::uint64_t key : missKeys)
            {
                checksum += map.find(key) == nullptr ? 1 : 0;
            }
        }
        break;
    case DetailedScenario::Erase:
        for (const std::uint64_t key : keys)
        {
            checksum += map.erase(key) ? 1 : 0;
        }
        break;
    case DetailedScenario::RehashOnly:
        map.reserve(count * 2);
        break;
    case DetailedScenario::CollisionHeavy:
        for (std::size_t index = 0; index < count; ++index)
        {
            map.emplace(keys[index], static_cast<std::uint64_t>(index));
        }
        for (const std::uint64_t key : keys)
        {
            const std::uint64_t* value = map.find(key);
            checksum += value == nullptr ? 0 : *value;
        }
        break;
    case DetailedScenario::TombstoneHeavy:
        for (std::size_t index = 0; index < count; ++index)
        {
            if ((index % 4) != 0)
            {
                map.emplace(missKeys[index], static_cast<std::uint64_t>(index));
            }
        }
        for (const std::uint64_t key : missKeys)
        {
            const std::uint64_t* value = map.find(key);
            checksum += value == nullptr ? 0 : *value;
        }
        break;
    }
    const auto end = Clock::now();

    const auto probeStatistics = map.GetProbeStatistics();
    DetailedRunResult result{};
    result.Microseconds = std::chrono::duration<double, std::micro>(end - start).count();
    result.Capacity = map.capacity();
    result.Tombstones = map.tombstone_count();
    if (probeStatistics.OccupiedCount != 0)
    {
        result.AverageProbe = static_cast<double>(probeStatistics.TotalProbeLength) /
            static_cast<double>(probeStatistics.OccupiedCount);
        result.CollisionRate = static_cast<double>(probeStatistics.CollidedCount) /
            static_cast<double>(probeStatistics.OccupiedCount);
    }
    result.MaximumProbe = probeStatistics.MaximumProbeLength;
    return result;
}

template<typename Hasher>
double RunStandardDetailedScenario(
    DetailedScenario scenario,
    const std::vector<std::uint64_t>& keys,
    const std::vector<std::uint64_t>& missKeys,
    std::uint64_t& checksum)
{
    using Clock = std::chrono::steady_clock;
    using StandardValue = std::pair<const std::uint64_t, std::uint64_t>;
    using StandardAllocator = StandardCountingAllocator<StandardValue>;
    using StandardMap = std::unordered_map<
        std::uint64_t,
        std::uint64_t,
        Hasher,
        std::equal_to<std::uint64_t>,
        StandardAllocator>;

    const std::size_t count = keys.size();
    StandardAllocationStatistics statistics{};
    StandardMap map(0, Hasher{}, std::equal_to<std::uint64_t>{}, StandardAllocator(statistics));
    if (scenario != DetailedScenario::InsertOnly &&
        scenario != DetailedScenario::CollisionHeavy)
    {
        map.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            map.emplace(keys[index], static_cast<std::uint64_t>(index));
        }
    }
    else
    {
        map.reserve(count);
    }

    if (scenario == DetailedScenario::TombstoneHeavy)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            if ((index % 4) != 0)
            {
                map.erase(keys[index]);
            }
        }
    }

    const auto start = Clock::now();
    switch (scenario)
    {
    case DetailedScenario::InsertOnly:
        for (std::size_t index = 0; index < count; ++index)
        {
            map.emplace(keys[index], static_cast<std::uint64_t>(index));
        }
        break;
    case DetailedScenario::FindHit:
        for (std::size_t repeat = 0; repeat < 8; ++repeat)
        {
            for (const std::uint64_t key : keys)
            {
                const auto found = map.find(key);
                checksum += found == map.end() ? 0 : found->second;
            }
        }
        break;
    case DetailedScenario::FindMiss:
        for (std::size_t repeat = 0; repeat < 8; ++repeat)
        {
            for (const std::uint64_t key : missKeys)
            {
                checksum += map.find(key) == map.end() ? 1 : 0;
            }
        }
        break;
    case DetailedScenario::Erase:
        for (const std::uint64_t key : keys)
        {
            checksum += map.erase(key);
        }
        break;
    case DetailedScenario::RehashOnly:
        map.reserve(count * 2);
        break;
    case DetailedScenario::CollisionHeavy:
        for (std::size_t index = 0; index < count; ++index)
        {
            map.emplace(keys[index], static_cast<std::uint64_t>(index));
        }
        for (const std::uint64_t key : keys)
        {
            const auto found = map.find(key);
            checksum += found == map.end() ? 0 : found->second;
        }
        break;
    case DetailedScenario::TombstoneHeavy:
        for (std::size_t index = 0; index < count; ++index)
        {
            if ((index % 4) != 0)
            {
                map.emplace(missKeys[index], static_cast<std::uint64_t>(index));
            }
        }
        for (const std::uint64_t key : missKeys)
        {
            const auto found = map.find(key);
            checksum += found == map.end() ? 0 : found->second;
        }
        break;
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

inline void RunDetailedBenchmarks(
    const std::size_t* counts,
    std::size_t countLength,
    std::size_t warmups,
    std::size_t samples,
    std::uint64_t& checksum)
{
    constexpr DetailedScenario Scenarios[] = {
        DetailedScenario::InsertOnly,
        DetailedScenario::FindHit,
        DetailedScenario::FindMiss,
        DetailedScenario::Erase,
        DetailedScenario::RehashOnly,
        DetailedScenario::CollisionHeavy,
        DetailedScenario::TombstoneHeavy
    };

    for (std::size_t countIndex = 0; countIndex < countLength; ++countIndex)
    {
        const std::size_t count = counts[countIndex];
        std::vector<std::uint64_t> keys(count);
        std::vector<std::uint64_t> missKeys(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            keys[index] = static_cast<std::uint64_t>(index);
            missKeys[index] = static_cast<std::uint64_t>(count + index);
        }

        for (const DetailedScenario scenario : Scenarios)
        {
            std::vector<double> ravenTimes;
            std::vector<double> standardTimes;
            DetailedRunResult lastResult{};
            for (std::size_t sample = 0; sample < warmups + samples; ++sample)
            {
                if (scenario == DetailedScenario::CollisionHeavy)
                {
                    lastResult = RunRavenDetailedScenario<GroupedHasher>(
                        scenario, keys, missKeys, checksum);
                    const double standardTime = RunStandardDetailedScenario<GroupedHasher>(
                        scenario, keys, missKeys, checksum);
                    if (sample >= warmups)
                    {
                        ravenTimes.push_back(lastResult.Microseconds);
                        standardTimes.push_back(standardTime);
                    }
                }
                else
                {
                    lastResult = RunRavenDetailedScenario<std::hash<std::uint64_t>>(
                        scenario, keys, missKeys, checksum);
                    const double standardTime = RunStandardDetailedScenario<std::hash<std::uint64_t>>(
                        scenario, keys, missKeys, checksum);
                    if (sample >= warmups)
                    {
                        ravenTimes.push_back(lastResult.Microseconds);
                        standardTimes.push_back(standardTime);
                    }
                }
            }

            const double ravenMedian = Median(ravenTimes);
            const double standardMedian = Median(standardTimes);
            std::cout << "[FlatHashMap Detail] count=" << count
                << " scenario=" << GetDetailedScenarioName(scenario)
                << " Raven_us=" << ravenMedian
                << " std_us=" << standardMedian
                << " Raven_over_std=" << (standardMedian == 0.0 ? 0.0 : ravenMedian / standardMedian)
                << " capacity=" << lastResult.Capacity
                << " tombstones=" << lastResult.Tombstones
                << " average_probe=" << lastResult.AverageProbe
                << " max_probe=" << lastResult.MaximumProbe
                << " collision_rate=" << lastResult.CollisionRate << '\n';
        }
    }
}

inline void RunTombstoneCleanupAnalysis(
    const std::size_t* counts,
    std::size_t countLength,
    std::size_t warmups,
    std::size_t samples)
{
    using Clock = std::chrono::steady_clock;
    for (std::size_t countIndex = 0; countIndex < countLength; ++countIndex)
    {
        const std::size_t count = counts[countIndex];
        std::vector<double> retainedTimes;
        std::vector<double> cleanedTimes;
        std::vector<double> cleanupTimes;
        DetailedRunResult retainedResult{};
        DetailedRunResult cleanedResult{};

        for (std::size_t sample = 0; sample < warmups + samples; ++sample)
        {
            FrameAllocator retainedAllocator(count * 1024 + 65536);
            FlatHashMap<std::uint64_t, std::uint64_t> retainedMap(retainedAllocator);
            retainedMap.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                retainedMap.emplace(index, index);
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                if ((index % 4) != 0)
                {
                    retainedMap.erase(index);
                }
            }
            const auto retainedStart = Clock::now();
            for (std::size_t index = 0; index < count; ++index)
            {
                if ((index % 4) != 0)
                {
                    retainedMap.emplace(count + index, index);
                }
            }
            const auto retainedEnd = Clock::now();

            FrameAllocator cleanedAllocator(count * 1024 + 65536);
            FlatHashMap<std::uint64_t, std::uint64_t> cleanedMap(cleanedAllocator);
            cleanedMap.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                cleanedMap.emplace(index, index);
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                if ((index % 4) != 0)
                {
                    cleanedMap.erase(index);
                }
            }
            const auto cleanupStart = Clock::now();
            cleanedMap.cleanup_tombstones();
            const auto cleanupEnd = Clock::now();
            const auto cleanedStart = Clock::now();
            for (std::size_t index = 0; index < count; ++index)
            {
                if ((index % 4) != 0)
                {
                    cleanedMap.emplace(count + index, index);
                }
            }
            const auto cleanedEnd = Clock::now();

            const auto retainedProbes = retainedMap.GetProbeStatistics();
            retainedResult.Tombstones = retainedMap.tombstone_count();
            retainedResult.MaximumProbe = retainedProbes.MaximumProbeLength;
            retainedResult.AverageProbe = retainedProbes.OccupiedCount == 0
                ? 0.0
                : static_cast<double>(retainedProbes.TotalProbeLength) /
                    static_cast<double>(retainedProbes.OccupiedCount);
            const auto cleanedProbes = cleanedMap.GetProbeStatistics();
            cleanedResult.Tombstones = cleanedMap.tombstone_count();
            cleanedResult.MaximumProbe = cleanedProbes.MaximumProbeLength;
            cleanedResult.AverageProbe = cleanedProbes.OccupiedCount == 0
                ? 0.0
                : static_cast<double>(cleanedProbes.TotalProbeLength) /
                    static_cast<double>(cleanedProbes.OccupiedCount);

            if (sample >= warmups)
            {
                retainedTimes.push_back(std::chrono::duration<double, std::micro>(
                    retainedEnd - retainedStart).count());
                cleanupTimes.push_back(std::chrono::duration<double, std::micro>(
                    cleanupEnd - cleanupStart).count());
                cleanedTimes.push_back(std::chrono::duration<double, std::micro>(
                    cleanedEnd - cleanedStart).count());
            }
        }

        std::cout << "[FlatHashMap Tombstone Analysis] count=" << count
            << " retained_insert_us=" << Median(retainedTimes)
            << " cleanup_us=" << Median(cleanupTimes)
            << " cleaned_insert_us=" << Median(cleanedTimes)
            << " retained_tombstones=" << retainedResult.Tombstones
            << " cleaned_tombstones=" << cleanedResult.Tombstones
            << " retained_average_probe=" << retainedResult.AverageProbe
            << " cleaned_average_probe=" << cleanedResult.AverageProbe
            << " retained_max_probe=" << retainedResult.MaximumProbe
            << " cleaned_max_probe=" << cleanedResult.MaximumProbe << '\n';
    }
}

inline void RunGrowthAnalysis(
    const std::size_t* counts,
    std::size_t countLength,
    std::size_t warmups,
    std::size_t samples)
{
    using Clock = std::chrono::steady_clock;
    for (std::size_t countIndex = 0; countIndex < countLength; ++countIndex)
    {
        const std::size_t count = counts[countIndex];
        std::vector<double> cumulativeTimes;
        std::vector<double> maximumTimes;
        std::vector<double> allocationTimes;
        std::vector<double> deallocationTimes;
        std::size_t rehashCount = 0;
        std::size_t hashCalls = 0;
        std::size_t allocationCount = 0;
        std::size_t peakBytes = 0;
        std::size_t capacity = 0;
        double loadFactor = 0.0;
        double averageProbe = 0.0;
        std::size_t maximumProbe = 0;

        for (std::size_t sample = 0; sample < warmups + samples; ++sample)
        {
            HashCallStatistics hashStatistics{};
            TimedFrameAllocator allocator(count * 1024 + 65536);
            FlatHashMap<std::uint64_t, std::uint64_t, CountingHasher> map(
                allocator,
                CountingHasher{ &hashStatistics });
            std::size_t sampleRehashCount = 0;
            double cumulativeRehashMicroseconds = 0.0;
            double maximumRehashMicroseconds = 0.0;
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::size_t previousCapacity = map.capacity();
                const auto start = Clock::now();
                map.emplace(static_cast<std::uint64_t>(index), static_cast<std::uint64_t>(index));
                const auto end = Clock::now();
                if (map.capacity() != previousCapacity)
                {
                    const double elapsed = std::chrono::duration<double, std::micro>(end - start).count();
                    cumulativeRehashMicroseconds += elapsed;
                    maximumRehashMicroseconds = (std::max)(maximumRehashMicroseconds, elapsed);
                    ++sampleRehashCount;
                }
            }
            hashCalls = hashStatistics.Calls;
            const auto probeStatistics = map.GetProbeStatistics();
            rehashCount = sampleRehashCount;
            allocationCount = allocator.GetAllocationCount();
            peakBytes = allocator.GetPeakUsedMemory();
            capacity = map.capacity();
            loadFactor = static_cast<double>(map.size()) / static_cast<double>(map.capacity());
            averageProbe = probeStatistics.OccupiedCount == 0
                ? 0.0
                : static_cast<double>(probeStatistics.TotalProbeLength) /
                    static_cast<double>(probeStatistics.OccupiedCount);
            maximumProbe = probeStatistics.MaximumProbeLength;
            if (sample >= warmups)
            {
                cumulativeTimes.push_back(cumulativeRehashMicroseconds);
                maximumTimes.push_back(maximumRehashMicroseconds);
                allocationTimes.push_back(allocator.GetTotalAllocationMicroseconds());
                deallocationTimes.push_back(allocator.GetTotalDeallocationMicroseconds());
            }
        }
        std::cout << "[FlatHashMap Growth Analysis] count=" << count
            << " rehashes=" << rehashCount
            << " cumulative_rehash_us=" << Median(cumulativeTimes)
            << " max_rehash_us=" << Median(maximumTimes)
            << " hash_calls=" << hashCalls
            << " allocation_us=" << Median(allocationTimes)
            << " deallocation_us=" << Median(deallocationTimes)
            << " allocations=" << allocationCount
            << " peak_bytes=" << peakBytes
            << " capacity=" << capacity
            << " load_factor=" << loadFactor
            << " average_probe=" << averageProbe
            << " max_probe=" << maximumProbe << '\n';
    }
}
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
    std::uint64_t detailedChecksum = 0;
    detail::RunDetailedBenchmarks(
        Counts,
        sizeof(Counts) / sizeof(Counts[0]),
        Warmups,
        Samples,
        detailedChecksum);
    detail::RunTombstoneCleanupAnalysis(
        Counts,
        sizeof(Counts) / sizeof(Counts[0]),
        Warmups,
        Samples);
    detail::RunGrowthAnalysis(
        Counts,
        sizeof(Counts) / sizeof(Counts[0]),
        Warmups,
        Samples);
    std::cout << "[FlatHashMap Benchmark] checksum=" << checksum << '\n';
    std::cout << "[FlatHashMap Detail] checksum=" << detailedChecksum << '\n';
    return 0;
}
} // namespace Raven::benchmarks
