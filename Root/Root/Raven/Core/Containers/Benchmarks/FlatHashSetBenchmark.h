#pragma once

#include "Raven/Core/Containers/FlatHashSet.h"
#include "Raven/Core/Memory/FrameAllocator.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <vector>

namespace Raven::benchmarks
{

// GPUを起動せず、同じ入力を繰り返して中央値を出力するCPU Benchmarkです。
// std::unordered_setはRaven Allocatorとは異なるallocation policyであることに注意してください。
inline int RunFlatHashSetBenchmark()
{
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t Warmups = 3;
    constexpr std::size_t Samples = 9;
    constexpr std::size_t Counts[] = { 128, 1024, 8192 };
    volatile std::uint64_t sink = 0;

    auto median = [](std::vector<double>& measurements)
    {
        std::sort(measurements.begin(), measurements.end());
        return measurements[measurements.size() / 2];
    };

    for (const std::size_t count : Counts)
    {
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            std::vector<double> ravenTimes;
            std::vector<double> standardTimes;
            std::size_t lastAllocations = 0;
            std::size_t lastPeakBytes = 0;
            std::size_t lastCapacity = 0;
            std::size_t lastMaxProbe = 0;

            for (std::size_t sample = 0; sample < Warmups + Samples; ++sample)
            {
                // FrameAllocatorは各サンプルで作り直し、前回のArena使用量を混入させません。
                FrameAllocator allocator(count * 256 + 65536);
                const auto ravenStart = Clock::now();
                {
                    FlatHashSet<std::uint64_t> values(allocator);
                    if (scenario != 1)
                    {
                        values.reserve(count);
                    }
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        values.insert(static_cast<std::uint64_t>(i * 2654435761ULL));
                    }
                    if (scenario == 2)
                    {
                        // erase/insertを繰り返してTombstoneの再利用と再配置コストを測ります。
                        for (std::size_t i = 0; i < count; i += 2)
                        {
                            values.erase(static_cast<std::uint64_t>(i * 2654435761ULL));
                        }
                        for (std::size_t i = 0; i < count; i += 2)
                        {
                            values.insert(static_cast<std::uint64_t>((i + count) * 2654435761ULL));
                        }
                    }
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        sink = sink + static_cast<std::uint64_t>(
                            values.contains(static_cast<std::uint64_t>(i * 2654435761ULL)));
                    }
                    lastCapacity = values.capacity();
                    lastMaxProbe = values.GetProbeStatistics().MaximumProbeLength;
                }
                const auto ravenEnd = Clock::now();
                lastAllocations = allocator.GetAllocationCount();
                lastPeakBytes = allocator.GetPeakUsedMemory();

                const auto standardStart = Clock::now();
                {
                    std::unordered_set<std::uint64_t> values;
                    if (scenario != 1)
                    {
                        values.reserve(count);
                    }
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        values.insert(static_cast<std::uint64_t>(i * 2654435761ULL));
                    }
                    if (scenario == 2)
                    {
                        for (std::size_t i = 0; i < count; i += 2)
                        {
                            values.erase(static_cast<std::uint64_t>(i * 2654435761ULL));
                        }
                        for (std::size_t i = 0; i < count; i += 2)
                        {
                            values.insert(static_cast<std::uint64_t>((i + count) * 2654435761ULL));
                        }
                    }
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        sink = sink + static_cast<std::uint64_t>(
                            values.contains(static_cast<std::uint64_t>(i * 2654435761ULL)));
                    }
                }
                const auto standardEnd = Clock::now();

                if (sample >= Warmups)
                {
                    ravenTimes.push_back(std::chrono::duration<double, std::micro>(
                        ravenEnd - ravenStart).count());
                    standardTimes.push_back(std::chrono::duration<double, std::micro>(
                        standardEnd - standardStart).count());
                }
            }

            const char* scenarioName = scenario == 0 ? "reserved" :
                (scenario == 1 ? "growth" : "erase-insert");
            std::cout << "[FlatHashSet Benchmark] count=" << count
                << " scenario=" << scenarioName
                << " Raven_us=" << median(ravenTimes)
                << " std_us=" << median(standardTimes)
                << " allocations=" << lastAllocations
                << " peak_bytes=" << lastPeakBytes
                << " capacity=" << lastCapacity
                << " max_probe=" << lastMaxProbe << '\n';
        }
    }
    // 最適化でLookup結果が消去されないように使用します。
    std::cout << "[FlatHashSet Benchmark] checksum=" << sink << '\n';
    return 0;
}

} // namespace Raven::benchmarks
