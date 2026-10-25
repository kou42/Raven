#pragma once

#include "Raven/Core/Containers/FlatHashSet.h"
#include "Raven/Core/Memory/FrameAllocator.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <unordered_set>
#include <vector>

namespace Raven::benchmarks
{
namespace detail
{

struct AllocationStatistics
{
    std::size_t AllocationCount = 0;
    std::size_t CurrentBytes = 0;
    std::size_t PeakBytes = 0;
};

// std::unordered_setのNodeとBucket配列を同じ集計先へ記録するAllocatorです。
// FrameAllocatorとは確保方式が異なるため、時間に加えてallocation回数とpeak byteを比較します。
template<typename T>
class CountingAllocator
{
public:
    using value_type = T;

    CountingAllocator() noexcept = default;
    explicit CountingAllocator(AllocationStatistics& statistics) noexcept
        : m_Statistics(&statistics)
    {
    }

    template<typename Other>
    CountingAllocator(const CountingAllocator<Other>& other) noexcept
        : m_Statistics(other.m_Statistics)
    {
    }

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
        {
            throw std::bad_array_new_length{};
        }

        const std::size_t bytes = count * sizeof(T);
        T* memory = static_cast<T*>(::operator new(bytes));
        if (m_Statistics != nullptr)
        {
            ++m_Statistics->AllocationCount;
            m_Statistics->CurrentBytes += bytes;
            if (m_Statistics->CurrentBytes > m_Statistics->PeakBytes)
            {
                m_Statistics->PeakBytes = m_Statistics->CurrentBytes;
            }
        }
        return memory;
    }

    void deallocate(T* memory, std::size_t count) noexcept
    {
        if (m_Statistics != nullptr)
        {
            m_Statistics->CurrentBytes -= count * sizeof(T);
        }
        ::operator delete(memory);
    }

    template<typename Other>
    bool operator==(const CountingAllocator<Other>& other) const noexcept
    {
        return m_Statistics == other.m_Statistics;
    }

    template<typename Other>
    bool operator!=(const CountingAllocator<Other>& other) const noexcept
    {
        return (*this == other) == false;
    }

private:
    template<typename>
    friend class CountingAllocator;

    AllocationStatistics* m_Statistics = nullptr;
};

enum class FlatHashSetScenario
{
    Reserved,
    Growth,
    EraseInsert
};

inline const char* GetScenarioName(FlatHashSetScenario scenario)
{
    if (scenario == FlatHashSetScenario::Reserved)
    {
        return "reserved";
    }
    if (scenario == FlatHashSetScenario::Growth)
    {
        return "growth";
    }
    return "erase-insert";
}

template<typename Set>
inline void RunOperations(
    Set& values,
    FlatHashSetScenario scenario,
    const std::vector<std::uint64_t>& inputKeys,
    const std::vector<std::uint64_t>& replacementKeys,
    volatile std::uint64_t& sink)
{
    if (scenario != FlatHashSetScenario::Growth)
    {
        values.reserve(inputKeys.size());
    }

    for (const std::uint64_t key : inputKeys)
    {
        values.insert(key);
    }

    if (scenario == FlatHashSetScenario::EraseInsert)
    {
        for (std::size_t i = 0; i < inputKeys.size(); i += 2)
        {
            values.erase(inputKeys[i]);
        }
        for (const std::uint64_t key : replacementKeys)
        {
            values.insert(key);
        }
    }

    // C++17のstd::unordered_setにはcontains()がないため、両Containerでfind()を使います。
    for (const std::uint64_t key : inputKeys)
    {
        sink = sink + static_cast<std::uint64_t>(values.find(key) != values.end());
    }
}

// FlatHashSet::find()のpointer APIを共通計測処理へ合わせる薄いAdapterです。
class FlatHashSetBenchmarkAdapter
{
public:
    explicit FlatHashSetBenchmarkAdapter(FrameAllocator& allocator)
        : m_Values(allocator)
    {
    }

    void reserve(std::size_t count) { m_Values.reserve(count); }
    void insert(std::uint64_t key) { m_Values.insert(key); }
    void erase(std::uint64_t key) { m_Values.erase(key); }
    const std::uint64_t* find(std::uint64_t key) const { return m_Values.find(key); }
    const std::uint64_t* end() const { return nullptr; }

    [[nodiscard]] const FlatHashSet<std::uint64_t>& GetValues() const { return m_Values; }

private:
    FlatHashSet<std::uint64_t> m_Values;
};

} // namespace detail

// GPUを起動せず、同じ入力・処理・warm-up回数で中央値を出力するCPU Benchmarkです。
// 診断統計の走査とContainer破棄は計測終了後に行い、本来の操作時間へ混入させません。
inline int RunFlatHashSetBenchmark()
{
    using Clock = std::chrono::steady_clock;
    using StandardSet = std::unordered_set<
        std::uint64_t,
        std::hash<std::uint64_t>,
        std::equal_to<std::uint64_t>,
        detail::CountingAllocator<std::uint64_t>>;

    constexpr std::size_t Warmups = 3;
    constexpr std::size_t Samples = 9;
    constexpr std::size_t Counts[] = { 128, 1024, 8192, 65536 };
    constexpr detail::FlatHashSetScenario Scenarios[] = {
        detail::FlatHashSetScenario::Reserved,
        detail::FlatHashSetScenario::Growth,
        detail::FlatHashSetScenario::EraseInsert
    };
    volatile std::uint64_t sink = 0;

    auto median = [](std::vector<double>& measurements)
    {
        std::sort(measurements.begin(), measurements.end());
        return measurements[measurements.size() / 2];
    };

    std::cout << "[FlatHashSet Benchmark] timing=reserve/insert/erase/find; "
        "diagnostics-and-destruction=excluded; warmups=" << Warmups
        << " samples=" << Samples << '\n';
    std::cout << "[FlatHashSet Benchmark] allocator_note=Raven peak is FrameAllocator arena usage; "
        "std peak/count include bucket and node allocations via CountingAllocator.\n";

    for (const std::size_t count : Counts)
    {
        std::vector<std::uint64_t> inputKeys;
        std::vector<std::uint64_t> replacementKeys;
        inputKeys.reserve(count);
        replacementKeys.reserve(count / 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            inputKeys.push_back(static_cast<std::uint64_t>(i * 2654435761ULL));
            if ((i % 2) == 0)
            {
                replacementKeys.push_back(
                    static_cast<std::uint64_t>((i + count) * 2654435761ULL));
            }
        }

        for (const detail::FlatHashSetScenario scenario : Scenarios)
        {
            std::vector<double> ravenTimes;
            std::vector<double> standardTimes;
            std::vector<double> tombstoneInsertTimes;
            std::vector<double> cleanedInsertTimes;
            std::size_t ravenAllocations = 0;
            std::size_t ravenPeakBytes = 0;
            std::size_t ravenCapacity = 0;
            std::size_t ravenMaximumProbe = 0;
            double ravenAverageProbe = 0.0;
            std::size_t ravenTombstones = 0;
            std::size_t standardAllocations = 0;
            std::size_t standardPeakBytes = 0;
            std::size_t standardCapacity = 0;

            for (std::size_t sample = 0; sample < Warmups + Samples; ++sample)
            {
                // backing buffer生成は計測外とし、Container操作から呼ばれるArena確保を計測します。
                FrameAllocator allocator(count * 256 + 65536);
                detail::FlatHashSetBenchmarkAdapter ravenValues(allocator);
                const auto ravenStart = Clock::now();
                detail::RunOperations(ravenValues, scenario, inputKeys, replacementKeys, sink);
                const auto ravenEnd = Clock::now();

                // Probe統計は全Slot走査を伴うため、必ず時間計測後に取得します。
                const auto probeStatistics = ravenValues.GetValues().GetProbeStatistics();
                ravenAllocations = allocator.GetAllocationCount();
                ravenPeakBytes = allocator.GetPeakUsedMemory();
                ravenCapacity = ravenValues.GetValues().capacity();
                ravenMaximumProbe = probeStatistics.MaximumProbeLength;
                ravenAverageProbe = probeStatistics.OccupiedCount == 0
                    ? 0.0
                    : static_cast<double>(probeStatistics.TotalProbeLength)
                        / static_cast<double>(probeStatistics.OccupiedCount);
                ravenTombstones = ravenValues.GetValues().tombstone_count();

                detail::AllocationStatistics standardStatistics{};
                StandardSet standardValues(
                    0,
                    std::hash<std::uint64_t>{},
                    std::equal_to<std::uint64_t>{},
                    detail::CountingAllocator<std::uint64_t>{standardStatistics});
                const auto standardStart = Clock::now();
                detail::RunOperations(standardValues, scenario, inputKeys, replacementKeys, sink);
                const auto standardEnd = Clock::now();

                standardAllocations = standardStatistics.AllocationCount;
                standardPeakBytes = standardStatistics.PeakBytes;
                standardCapacity = standardValues.bucket_count();

                if (scenario == detail::FlatHashSetScenario::EraseInsert)
                {
                    // Tombstoneを保持した場合と、同容量rehashで整理した場合のinsertだけを
                    // 別々のContainerで測り、cleanup自体の時間を混入させず影響を比較します。
                    FrameAllocator tombstoneAllocator(count * 256 + 65536);
                    FlatHashSet<std::uint64_t> tombstoneValues(tombstoneAllocator);
                    tombstoneValues.reserve(count);
                    for (const std::uint64_t key : inputKeys)
                    {
                        tombstoneValues.insert(key);
                    }
                    for (std::size_t i = 0; i < inputKeys.size(); i += 2)
                    {
                        tombstoneValues.erase(inputKeys[i]);
                    }
                    const auto tombstoneInsertStart = Clock::now();
                    for (const std::uint64_t key : replacementKeys)
                    {
                        tombstoneValues.insert(key);
                    }
                    const auto tombstoneInsertEnd = Clock::now();

                    FrameAllocator cleanedAllocator(count * 256 + 65536);
                    FlatHashSet<std::uint64_t> cleanedValues(cleanedAllocator);
                    cleanedValues.reserve(count);
                    for (const std::uint64_t key : inputKeys)
                    {
                        cleanedValues.insert(key);
                    }
                    for (std::size_t i = 0; i < inputKeys.size(); i += 2)
                    {
                        cleanedValues.erase(inputKeys[i]);
                    }
                    cleanedValues.cleanup_tombstones();
                    const auto cleanedInsertStart = Clock::now();
                    for (const std::uint64_t key : replacementKeys)
                    {
                        cleanedValues.insert(key);
                    }
                    const auto cleanedInsertEnd = Clock::now();

                    if (sample >= Warmups)
                    {
                        tombstoneInsertTimes.push_back(std::chrono::duration<double, std::micro>(
                            tombstoneInsertEnd - tombstoneInsertStart).count());
                        cleanedInsertTimes.push_back(std::chrono::duration<double, std::micro>(
                            cleanedInsertEnd - cleanedInsertStart).count());
                    }
                }

                if (sample >= Warmups)
                {
                    ravenTimes.push_back(std::chrono::duration<double, std::micro>(
                        ravenEnd - ravenStart).count());
                    standardTimes.push_back(std::chrono::duration<double, std::micro>(
                        standardEnd - standardStart).count());
                }
            }

            const double ravenMedian = median(ravenTimes);
            const double standardMedian = median(standardTimes);
            const double ratio = standardMedian == 0.0 ? 0.0 : ravenMedian / standardMedian;
            double tombstoneInsertMedian = 0.0;
            double cleanedInsertMedian = 0.0;
            double tombstoneOverCleaned = 0.0;
            if (scenario == detail::FlatHashSetScenario::EraseInsert)
            {
                tombstoneInsertMedian = median(tombstoneInsertTimes);
                cleanedInsertMedian = median(cleanedInsertTimes);
                tombstoneOverCleaned = cleanedInsertMedian == 0.0
                    ? 0.0
                    : tombstoneInsertMedian / cleanedInsertMedian;
            }
            std::cout << "[FlatHashSet Benchmark] count=" << count
                << " scenario=" << detail::GetScenarioName(scenario)
                << " Raven_us=" << ravenMedian
                << " std_us=" << standardMedian
                << " Raven_over_std=" << ratio
                << " Raven_allocations=" << ravenAllocations
                << " std_allocations=" << standardAllocations
                << " Raven_peak_bytes=" << ravenPeakBytes
                << " std_peak_bytes=" << standardPeakBytes
                << " Raven_capacity=" << ravenCapacity
                << " std_buckets=" << standardCapacity
                << " max_probe=" << ravenMaximumProbe
                << " average_probe=" << ravenAverageProbe
                << " tombstones=" << ravenTombstones;
            if (scenario == detail::FlatHashSetScenario::EraseInsert)
            {
                std::cout << " tombstone_insert_us=" << tombstoneInsertMedian
                    << " cleaned_insert_us=" << cleanedInsertMedian
                    << " tombstone_over_cleaned=" << tombstoneOverCleaned;
            }
            std::cout << '\n';
        }
    }
    // 最適化でLookup結果が消去されないように使用します。
    std::cout << "[FlatHashSet Benchmark] checksum=" << sink << '\n';
    return 0;
}

} // namespace Raven::benchmarks
