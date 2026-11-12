#pragma once
#include <cstddef>
#include <cstdint>
namespace Raven
{
namespace Data
{
namespace Tests
{
struct BenchmarkMemory
{
    std::uint64_t Allocations = 0;
    std::uint64_t Bytes = 0;
    std::uint64_t PeakBytes = 0;
};
// 専用実行ファイルだけで有効。開始時点の既存領域を除くC++要求Byte数を全Threadで集計します。
// 同時Scopeは不可。対象Jobを全回収後にEndしてください。malloc/OS/GPU/追跡Headerは対象外です。
void BeginBenchmarkMemory();
BenchmarkMemory EndBenchmarkMemory();
void FailBenchmarkAllocationAfter(std::int64_t count);
}
}
}
