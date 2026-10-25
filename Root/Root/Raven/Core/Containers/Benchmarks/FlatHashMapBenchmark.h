#pragma once

#include "Raven/Core/Containers/FlatHashMap.h"
#include "Raven/Core/Memory/FrameAllocator.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace Raven::benchmarks
{
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

                std::unordered_map<std::uint64_t, std::uint64_t> standard;
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
                << " std_buckets=" << standardBuckets << '\n';
        }
    }
    std::cout << "[FlatHashMap Benchmark] checksum=" << checksum << '\n';
    return 0;
}
} // namespace Raven::benchmarks
