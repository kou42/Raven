#include "Raven/Data/Tests/DataBenchmarkMemory.h"
#include "Raven/Data/Tests/ParallelDeserializeTests.h"
#include "Raven/Data/Tests/MutableDataDocumentTests.h"
#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include "Raven/Data/Serialization/ParallelDeserialize.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/Json/JsonWriter.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <new>
#include <stdexcept>

#ifdef RAVEN_DATA_BENCHMARK_MAIN
namespace Raven { namespace Data { namespace Tests {
namespace {
volatile std::size_t benchmarkSink = 0;
std::string Rows(std::size_t count, std::size_t values, bool escaped)
{
    std::string row = "{\"type\":\"Raven.StructuredDataTest\",\"version\":2,\"enabled\":true,\"label\":\"";
    row += escaped ? "\\u65e5\\u672c\\u8a9e\\n\\u65e5\\u672c\\u8a9e" : "sample";
    row += "\",\"values\":[";
    for (std::size_t i = 0; i < values; ++i) { if (i != 0) { row += ','; } row += std::to_string(i); }
    row += "]}";
    std::string text = "[";
    for (std::size_t i = 0; i < count; ++i) { if (i != 0) { text += ','; } text += row; }
    return text + "]";
}
void Require(bool value)
{
    if (value == false) { throw std::runtime_error("benchmark operation failed"); }
}
void Measure(const std::string& name, const char* stage, std::size_t workers, std::size_t elements,
    std::size_t tokens, const std::function<bool()>& operation)
{
    Require(operation());
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
    BeginBenchmarkMemory();
    const bool success = operation();
    const auto memory = EndBenchmarkMemory();
    Require(success);
    std::cout << name << ',' << stage << ',' << workers << ',' << elements << ',' << tokens
        << ",NA," << memory.Allocations << ',' << memory.Bytes << ',' << memory.PeakBytes << '\n';
#else
    std::vector<double> times;
    for (int i = 0; i < 7; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        Require(operation());
        const auto end = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    std::cout << name << ',' << stage << ',' << workers << ',' << elements << ',' << tokens
        << ',' << times[times.size() / 2] << ",NA,NA,NA\n";
#endif
}
bool Decode(DataElement root)
{
    std::vector<DataElement> pending{root};
    std::vector<std::string> strings;
    while (pending.empty() == false)
    {
        const auto element = pending.back(); pending.pop_back();
        if (element.IsType(DataType::String))
        {
            std::string decoded;
            if (element.TryGetDecodedString(decoded) == false) { return false; }
            strings.push_back(std::move(decoded));
        }
        for (auto child = element.GetFirstChild(); child.IsValid(); child = child.GetNextSibling()) { pending.push_back(child); }
    }
    benchmarkSink = strings.size();
    return true;
}
void Stages(const std::string& name, const std::string& text, bool deep, JobSystem& pool)
{
    DataDocument document;
    Require(JsonParser::Parse(text, document));
    const auto tokens = document.GetTokenCount();
    Measure(name, "parse", 0, 0, tokens, [&]()
    {
        DataDocument parsed;
        const bool success = JsonParser::Parse(text, parsed);
        benchmarkSink = parsed.GetTokenCount(); return success;
    });
    Measure(name, "decode", 0, 0, tokens, [&]() { return Decode(document.GetRoot()); });
    Measure(name, "convert", 1, 0, tokens, [&]()
    {
        if (deep)
        {
            DataElement leaf = document.GetRoot();
            while (leaf.IsType(DataType::Array)) { leaf = leaf.GetFirstChild(); }
            std::uint64_t value = 0;
            const bool success = Deserialize(leaf, value); benchmarkSink = static_cast<std::size_t>(value); return success;
        }
        std::vector<TestSettings> values;
        ArrayReadResult result;
        const bool success = DeserializeArray(document.GetRoot(), values, pool, result);
        benchmarkSink = values.size(); return success;
    });
    Measure(name, "write", 0, 0, tokens, [&]()
    {
        std::string output;
        const bool success = JsonWriter::Write(document, output);
        benchmarkSink = output.size(); return success;
    });
}
}
int RunStructuredDataBenchmarks()
{
    if (RunParallelDeserializeTests() != 0) { return 1; }
    if (RunMutableDataDocumentTests() != 0) { return 1; }
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
    // Peakを累積確保Byteと取り違えないこと、Aligned/SizedとThread跨ぎ解放を検証します。
    BeginBenchmarkMemory();
    void* first = ::operator new(100);
    ::operator delete(first);
    void* second = ::operator new(60, std::align_val_t(64));
    Require(reinterpret_cast<std::uintptr_t>(second) % 64 == 0);
    ::operator delete(second, std::align_val_t(64));
    const auto known = EndBenchmarkMemory();
    Require(known.Allocations == 2 && known.Bytes == 160 && known.PeakBytes == 100);
    BeginBenchmarkMemory();
    void* previousSession = ::operator new(256);
    EndBenchmarkMemory();
    BeginBenchmarkMemory();
    ::operator delete(previousSession);
    void* currentSession = ::operator new(32);
    ::operator delete(currentSession);
    const auto reset = EndBenchmarkMemory();
    Require(reset.Allocations == 1 && reset.PeakBytes == 32 && reset.Bytes == 32);
    JobSystem memoryPool(2);
    std::atomic<void*> shared{nullptr};
    BeginBenchmarkMemory();
    auto producer = memoryPool.Submit([&]() { shared.store(::operator new(4096)); });
    producer.get();
    auto consumer = memoryPool.Submit([&]() { ::operator delete(shared.load()); });
    consumer.get();
    const auto threaded = EndBenchmarkMemory();
    Require(threaded.Allocations >= 1 && threaded.Bytes >= 4096 && threaded.PeakBytes >= 4096);
    std::cout << "BenchmarkMemory: known peak and cross-thread allocation/free passed\n";
#endif
    std::cout << "case,stage,workers,elements,tokens,median_us,allocations,total_bytes,peak_bytes\n" << std::fixed << std::setprecision(3);
    JobSystem pool(4);
    Stages("small", Rows(1, 3, false), false, pool);
    Stages("large", Rows(4096, 32, false), false, pool);
    Stages("deep96", std::string(96, '[') + "1" + std::string(96, ']'), true, pool);
    Stages("escaped", Rows(4096, 3, true), false, pool);
    for (std::size_t payload : {0u, 64u})
    {
        for (std::size_t count : {32u, 128u, 512u, 2048u, 8192u})
        {
            DataDocument document;
            Require(JsonParser::Parse(Rows(count, payload, true), document));
            const std::string name = "rows" + std::to_string(count) + "_values" + std::to_string(payload);
            std::vector<TestSettings> reference;
            Require(Deserialize(document.GetRoot(), reference));
            Measure(name, "existing", 1, count, document.GetTokenCount(), [&]()
            {
                std::vector<TestSettings> output;
                const bool success = Deserialize(document.GetRoot(), output);
                benchmarkSink = output.size(); return success;
            });
            for (std::size_t workers : {1u, 2u, 4u, 8u})
            {
                JobSystem workersPool(workers);
                ArrayReadOptions options;
                options.EnableParallel = workers > 1;
                // 逐次と完全一致することを、時間計測の外でも確認します。
                std::vector<TestSettings> checked;
                ArrayReadResult result;
                Require(DeserializeArray(document.GetRoot(), checked, workersPool, result, options));
                Require(checked.size() == reference.size());
                for (std::size_t i = 0; i < checked.size(); ++i)
                {
                    Require(checked[i].Enabled == reference[i].Enabled && checked[i].Label == reference[i].Label
                        && checked[i].Values == reference[i].Values);
                }
                Measure(name, "array", workers, count, document.GetTokenCount(), [&]()
                {
                    std::vector<TestSettings> output;
                    const bool success = DeserializeArray(document.GetRoot(), output, workersPool, result, options);
                    benchmarkSink = output.size(); return success;
                });
            }
            // TestSettingsのこの実測負荷に限定した保守的な切替条件。
            // Adapterの処理量はToken数だけでは決まらないため、汎用APIの既定値にはしません。
            ArrayReadOptions measured;
            measured.EnableParallel = true;
            measured.MinElements = 128;
            measured.MinTokens = 1409;
            ArrayReadResult selected;
            Measure(name, "measured_policy", 4, count, document.GetTokenCount(), [&]()
            {
                std::vector<TestSettings> output;
                const bool success = DeserializeArray(document.GetRoot(), output, pool, selected, measured);
                Require(selected.UsedParallel == (count >= 128 && document.GetTokenCount() >= 1409));
                benchmarkSink = output.size(); return success;
            });
        }
    }
    return 0;
}
} } }
int main()
{
    try { return Raven::Data::Tests::RunStructuredDataBenchmarks(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif
