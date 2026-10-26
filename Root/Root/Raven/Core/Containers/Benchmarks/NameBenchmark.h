#pragma once

#include "Raven/Core/Containers/FlatHashMap.h"
#include "Raven/Core/Containers/Name.h"
#include "Raven/Core/Memory/HeapAllocator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Raven::benchmarks
{
namespace name_benchmark_detail
{

constexpr std::size_t WarmupCount = 3;
constexpr std::size_t SampleCount = 9;
constexpr std::size_t QueryPatternSize = 65536;
constexpr std::size_t QueryCount = 1 << 20;

inline double Median(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

template<typename Operation>
double MeasureNanosecondsPerQuery(Operation&& operation, std::uint64_t& checksum)
{
    using Clock = std::chrono::steady_clock;
    std::vector<double> samples;
    samples.reserve(SampleCount);

    for (std::size_t sample = 0; sample < WarmupCount + SampleCount; ++sample)
    {
        const auto start = Clock::now();
        const std::uint64_t localChecksum = operation();
        const auto end = Clock::now();
        checksum += localChecksum;

        if (sample >= WarmupCount)
        {
            const double nanoseconds =
                std::chrono::duration<double, std::nano>(end - start).count();
            samples.push_back(nanoseconds / static_cast<double>(QueryCount));
        }
    }
    return Median(samples);
}

inline std::vector<std::size_t> BuildQueryPattern(std::size_t nameCount)
{
    std::vector<std::size_t> queries;
    queries.reserve(QueryPatternSize);
    std::uint32_t state = 0x9e3779b9u;
    for (std::size_t index = 0; index < QueryPatternSize; ++index)
    {
        state = state * 1664525u + 1013904223u;
        queries.push_back(static_cast<std::size_t>(state) & (nameCount - 1));
    }
    return queries;
}

} // namespace name_benchmark_detail

inline int RunNameBenchmark()
{
    using namespace name_benchmark_detail;

    constexpr std::array<std::size_t, 3> NameCounts = { 32, 256, 2048 };
    std::uint64_t checksum = 0;
    std::cout << "[Name Benchmark] timing=median; warmups=" << WarmupCount
        << " samples=" << SampleCount
        << " queries_per_sample=" << QueryCount << '\n';

    for (const std::size_t nameCount : NameCounts)
    {
        std::vector<std::string> texts;
        texts.reserve(nameCount);
        for (std::size_t index = 0; index < nameCount; ++index)
        {
            // SSOに収まる短い名前だけへ偏らず、Animation/Asset識別子に近い長さを使用します。
            texts.push_back("AnimationState_" + std::to_string(index));
        }

        std::unordered_map<std::string, std::uint64_t> standardValues;
        standardValues.reserve(nameCount);
        for (std::size_t index = 0; index < nameCount; ++index)
        {
            standardValues.emplace(texts[index], static_cast<std::uint64_t>(index));
        }

        HeapAllocator allocator;
        NameTable nameTable(allocator);
        FlatHashMap<Name, std::uint64_t, NameHasher> nameValues(allocator);
        nameValues.reserve(nameCount);
        std::vector<Name> names;
        names.reserve(nameCount);
        for (std::size_t index = 0; index < nameCount; ++index)
        {
            const Name name = nameTable.Intern(StringView(texts[index]));
            names.push_back(name);
            nameValues.emplace(name, static_cast<std::uint64_t>(index));
        }

        const std::vector<std::size_t> queries = BuildQueryPattern(nameCount);
        const auto standardLookup = [&]()
        {
            std::uint64_t localChecksum = 0;
            for (std::size_t query = 0; query < QueryCount; ++query)
            {
                const std::size_t index = queries[query & (QueryPatternSize - 1)];
                const auto found = standardValues.find(texts[index]);
                if (found != standardValues.end())
                {
                    localChecksum += found->second;
                }
            }
            return localChecksum;
        };
        const auto nameLookup = [&]()
        {
            std::uint64_t localChecksum = 0;
            for (std::size_t query = 0; query < QueryCount; ++query)
            {
                const std::size_t index = queries[query & (QueryPatternSize - 1)];
                const std::uint64_t* found = nameValues.find(names[index]);
                if (found != nullptr)
                {
                    localChecksum += *found;
                }
            }
            return localChecksum;
        };
        const auto internThenLookup = [&]()
        {
            std::uint64_t localChecksum = 0;
            for (std::size_t query = 0; query < QueryCount; ++query)
            {
                const std::size_t index = queries[query & (QueryPatternSize - 1)];
                const Name name = nameTable.Intern(StringView(texts[index]));
                const std::uint64_t* found = nameValues.find(name);
                if (found != nullptr)
                {
                    localChecksum += *found;
                }
            }
            return localChecksum;
        };

        const double standardNanoseconds =
            MeasureNanosecondsPerQuery(standardLookup, checksum);
        const double nameNanoseconds = MeasureNanosecondsPerQuery(nameLookup, checksum);
        const double internNanoseconds =
            MeasureNanosecondsPerQuery(internThenLookup, checksum);

        std::cout << "[Name Benchmark] count=" << nameCount
            << " scenario=lookup-hit"
            << " std_string_ns=" << standardNanoseconds
            << " name_handle_ns=" << nameNanoseconds
            << " intern_then_name_ns=" << internNanoseconds
            << " name_over_std="
            << (standardNanoseconds == 0.0 ? 0.0 : nameNanoseconds / standardNanoseconds)
            << " intern_over_std="
            << (standardNanoseconds == 0.0 ? 0.0 : internNanoseconds / standardNanoseconds)
            << " name_live_allocations=" << allocator.GetAllocationCount()
            << " name_peak_bytes=" << allocator.GetPeakUsedMemory()
            << " std_buckets=" << standardValues.bucket_count()
            << " raven_capacity=" << nameValues.capacity() << '\n';
    }

    std::cout << "[Name Benchmark] checksum=" << checksum << '\n';
    return 0;
}

} // namespace Raven::benchmarks
