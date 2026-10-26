#pragma once

#include "Raven/Core/Containers/Array.h"
#include "Raven/Core/Containers/FixedVector.h"
#include "Raven/Core/Containers/SmallVector.h"
#include "Raven/Core/Memory/HeapAllocator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace Raven::benchmarks
{
namespace container_benchmark_detail
{
constexpr std::size_t InlineCount = 16;
constexpr std::size_t OverflowCount = 64;
constexpr std::size_t WarmupCount = 3;
constexpr std::size_t SampleCount = 9;
constexpr std::size_t FixedIterationCount = 100000;
constexpr std::size_t OverflowIterationCount = 10000;

#ifdef _MSC_VER
#define RAVEN_CONTAINER_BENCH_NOINLINE __declspec(noinline)
#else
#define RAVEN_CONTAINER_BENCH_NOINLINE __attribute__((noinline))
#endif

class CountingHeapAllocator final : public Allocator
{
public:
    void* Allocate(std::size_t size, std::size_t alignment) override
    {
        void* memory = m_Heap.Allocate(size, alignment);
        if (memory != nullptr)
        {
            ++m_TotalAllocationCount;
        }
        return memory;
    }

    void Deallocate(void* memory) override
    {
        if (memory != nullptr)
        {
            ++m_TotalDeallocationCount;
        }
        m_Heap.Deallocate(memory);
    }

    void Reset() override { m_Heap.Reset(); }
    [[nodiscard]] std::size_t GetCapacity() const override { return m_Heap.GetCapacity(); }
    [[nodiscard]] std::size_t GetUsedMemory() const override { return m_Heap.GetUsedMemory(); }
    [[nodiscard]] std::size_t GetPeakUsedMemory() const override { return m_Heap.GetPeakUsedMemory(); }
    [[nodiscard]] std::size_t GetAllocationCount() const override { return m_Heap.GetAllocationCount(); }
    [[nodiscard]] std::size_t GetTotalAllocationCount() const noexcept { return m_TotalAllocationCount; }
    [[nodiscard]] std::size_t GetTotalDeallocationCount() const noexcept { return m_TotalDeallocationCount; }

private:
    HeapAllocator m_Heap;
    std::size_t m_TotalAllocationCount = 0;
    std::size_t m_TotalDeallocationCount = 0;
};

RAVEN_CONTAINER_BENCH_NOINLINE inline std::uint64_t ConsumeRange(
    const std::uint64_t* values,
    std::size_t count)
{
    std::uint64_t checksum = 0;
    for (std::size_t index = 0; index < count; ++index)
    {
        checksum += values[index];
    }
    return checksum;
}

RAVEN_CONTAINER_BENCH_NOINLINE inline std::uint64_t RunStandardArrayWrite(std::uint64_t seed)
{
    std::array<std::uint64_t, InlineCount> values{};
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        values[index] = seed + index;
    }
    return ConsumeRange(values.data(), values.size());
}

RAVEN_CONTAINER_BENCH_NOINLINE inline std::uint64_t RunArrayWrite(std::uint64_t seed)
{
    Array<std::uint64_t, InlineCount> values;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        values[index] = seed + index;
    }
    return ConsumeRange(values.data(), values.size());
}

RAVEN_CONTAINER_BENCH_NOINLINE inline std::uint64_t RunFixedVectorAppend(std::uint64_t seed)
{
    FixedVector<std::uint64_t, InlineCount> values;
    for (std::size_t index = 0; index < InlineCount; ++index)
    {
        values.emplace_back(seed + index);
    }
    return ConsumeRange(values.data(), values.size());
}

RAVEN_CONTAINER_BENCH_NOINLINE inline std::uint64_t RunSmallVectorInlineAppend(
    CountingHeapAllocator& allocator,
    std::uint64_t seed)
{
    SmallVector<std::uint64_t, InlineCount> values(allocator);
    for (std::size_t index = 0; index < InlineCount; ++index)
    {
        values.emplace_back(seed + index);
    }
    return ConsumeRange(values.data(), values.size());
}

RAVEN_CONTAINER_BENCH_NOINLINE inline std::uint64_t RunSmallVectorOverflowAppend(
    CountingHeapAllocator& allocator,
    std::uint64_t seed)
{
    SmallVector<std::uint64_t, InlineCount> values(allocator);
    for (std::size_t index = 0; index < OverflowCount; ++index)
    {
        values.emplace_back(seed + index);
    }
    return ConsumeRange(values.data(), values.size());
}

inline double Median(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

template<typename Operation>
double MeasureOperation(
    std::size_t iterationCount,
    Operation&& operation,
    std::uint64_t& checksum)
{
    using Clock = std::chrono::steady_clock;
    std::vector<double> samples;
    samples.reserve(SampleCount);

    for (std::size_t sample = 0; sample < WarmupCount + SampleCount; ++sample)
    {
        std::uint64_t localChecksum = 0;
        const auto start = Clock::now();
        for (std::size_t iteration = 0; iteration < iterationCount; ++iteration)
        {
            localChecksum += operation(static_cast<std::uint64_t>(iteration));
        }
        const auto end = Clock::now();
        checksum += localChecksum;

        if (sample >= WarmupCount)
        {
            const double nanoseconds =
                std::chrono::duration<double, std::nano>(end - start).count();
            samples.push_back(nanoseconds / static_cast<double>(iterationCount));
        }
    }
    return Median(samples);
}

template<typename Container>
double MeasureTraversal(
    const Container& values,
    std::size_t iterationCount,
    std::uint64_t& checksum)
{
    return MeasureOperation(
        iterationCount,
        [&values](std::uint64_t iteration)
        {
            return ConsumeRange(values.data(), values.size()) + (iteration & 1u);
        },
        checksum);
}

template<typename Container>
void PrintLayout(const char* name, const Container& values)
{
    const auto address = reinterpret_cast<std::uintptr_t>(values.data());
    const bool dataAligned = (address % alignof(typename Container::value_type)) == 0;
    std::cout << "[Container Benchmark] layout=" << name
        << " sizeof=" << sizeof(Container)
        << " alignof=" << alignof(Container)
        << " value_alignment=" << alignof(typename Container::value_type)
        << " data_aligned=" << (dataAligned ? "true" : "false") << '\n';
}

inline void PrintTiming(const char* scenario, std::size_t count, double nanoseconds)
{
    std::cout << "[Container Benchmark] scenario=" << scenario
        << " count=" << count
        << " median_ns_per_iteration=" << nanoseconds << '\n';
}

#undef RAVEN_CONTAINER_BENCH_NOINLINE
} // namespace container_benchmark_detail

inline int RunContainerBenchmark()
{
    using namespace container_benchmark_detail;

    std::cout << "[Container Benchmark] timing=median; warmups=" << WarmupCount
        << " samples=" << SampleCount
        << " fixed_iterations=" << FixedIterationCount
        << " overflow_iterations=" << OverflowIterationCount << '\n';

    std::array<std::uint64_t, InlineCount> standardArray{};
    Array<std::uint64_t, InlineCount> array;
    FixedVector<std::uint64_t, InlineCount> fixed;
    CountingHeapAllocator inlineAllocator;
    SmallVector<std::uint64_t, InlineCount> smallInline(inlineAllocator);
    for (std::size_t index = 0; index < InlineCount; ++index)
    {
        standardArray[index] = index;
        array[index] = index;
        fixed.emplace_back(index);
        smallInline.emplace_back(index);
    }

    PrintLayout("std::array<uint64,16>", standardArray);
    PrintLayout("Array<uint64,16>", array);
    PrintLayout("FixedVector<uint64,16>", fixed);
    PrintLayout("SmallVector<uint64,16>", smallInline);

    std::uint64_t checksum = 0;
    PrintTiming(
        "std-array-construct-write",
        InlineCount,
        MeasureOperation(FixedIterationCount, RunStandardArrayWrite, checksum));
    PrintTiming(
        "array-construct-write",
        InlineCount,
        MeasureOperation(FixedIterationCount, RunArrayWrite, checksum));
    PrintTiming(
        "fixed-vector-construct-append",
        InlineCount,
        MeasureOperation(FixedIterationCount, RunFixedVectorAppend, checksum));

    CountingHeapAllocator timedInlineAllocator;
    PrintTiming(
        "small-vector-inline-construct-append",
        InlineCount,
        MeasureOperation(
            FixedIterationCount,
            [&timedInlineAllocator](std::uint64_t seed)
            {
                return RunSmallVectorInlineAppend(timedInlineAllocator, seed);
            },
            checksum));

    CountingHeapAllocator timedOverflowAllocator;
    PrintTiming(
        "small-vector-overflow-construct-append",
        OverflowCount,
        MeasureOperation(
            OverflowIterationCount,
            [&timedOverflowAllocator](std::uint64_t seed)
            {
                return RunSmallVectorOverflowAppend(timedOverflowAllocator, seed);
            },
            checksum));

    PrintTiming(
        "std-array-traverse",
        InlineCount,
        MeasureTraversal(standardArray, FixedIterationCount, checksum));
    PrintTiming(
        "array-traverse",
        InlineCount,
        MeasureTraversal(array, FixedIterationCount, checksum));
    PrintTiming(
        "fixed-vector-traverse",
        InlineCount,
        MeasureTraversal(fixed, FixedIterationCount, checksum));
    PrintTiming(
        "small-vector-inline-traverse",
        InlineCount,
        MeasureTraversal(smallInline, FixedIterationCount, checksum));

    CountingHeapAllocator allocationDiagnostics;
    {
        SmallVector<std::uint64_t, InlineCount> values(allocationDiagnostics);
        for (std::size_t index = 0; index < OverflowCount; ++index)
        {
            values.emplace_back(index);
        }
        std::cout << "[Container Benchmark] scenario=small-vector-overflow-allocation"
            << " count=" << values.size()
            << " capacity=" << values.capacity()
            << " total_allocations=" << allocationDiagnostics.GetTotalAllocationCount()
            << " live_allocations=" << allocationDiagnostics.GetAllocationCount()
            << " peak_bytes=" << allocationDiagnostics.GetPeakUsedMemory() << '\n';
    }
    std::cout << "[Container Benchmark] scenario=small-vector-overflow-release"
        << " total_deallocations=" << allocationDiagnostics.GetTotalDeallocationCount()
        << " live_allocations=" << allocationDiagnostics.GetAllocationCount() << '\n';
    std::cout << "[Container Benchmark] checksum=" << checksum << '\n';
    return 0;
}

} // namespace Raven::benchmarks
