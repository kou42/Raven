#include "Raven/Data/Tests/ParallelDeserializeTests.h"
#include "Raven/Data/Serialization/ParallelDeserialize.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include "Raven/Data/Tests/DataBenchmarkMemory.h"
#include <iostream>
#include <stdexcept>

namespace Raven { namespace Data { namespace Tests {
struct CancelValue { int Value = 0; };
std::atomic<bool>* cancellationForTest = nullptr;
}
template<> struct DataAdapter<Tests::CancelValue>
{
    static bool Read(DataElement input, Tests::CancelValue& output, SerializationContext& context);
};
bool DataAdapter<Tests::CancelValue>::Read(DataElement input, Tests::CancelValue& output, SerializationContext& context)
{
    if (DataAdapter<int>::Read(input, output.Value, context) == false) { return false; }
    if (output.Value == 7) { Tests::cancellationForTest->store(true); }
    if (output.Value == 9) { throw std::runtime_error("adapter failure"); }
    return true;
}
namespace Tests {
namespace {
int checks = 0, failures = 0;
void Check(bool value, const char* text)
{
    ++checks;
    if (value == false) { ++failures; std::cerr << "FAIL: " << text << '\n'; }
}
}
int RunParallelDeserializeTests()
{
    checks = 0; failures = 0;
    JobSystem pool(4);
    DataDocument document;
    ArrayReadResult result;
    ArrayReadOptions options;
    std::vector<int> output{99};
    Check(JsonParser::Parse("[0,1,2,3,4,5,6,7,8,9,10]", document), "array fixture");
    Check(DeserializeArray(document.GetRoot(), output, pool, result) && result.UsedParallel == false && output[10] == 10, "default sequential");
    options.EnableParallel = true;
    Check(DeserializeArray(document.GetRoot(), output, pool, result, options) && result.SubmittedJobs == 4
        && output == std::vector<int>({0,1,2,3,4,5,6,7,8,9,10}), "parallel stable order uneven chunks");
    options.MinElements = 12;
    Check(DeserializeArray(document.GetRoot(), output, pool, result, options) && result.UsedParallel == false, "element threshold");
    options.MinElements = 11; options.MinTokens = 13;
    Check(DeserializeArray(document.GetRoot(), output, pool, result, options) && result.UsedParallel == false, "token threshold");
    options.MinTokens = 12;
    Check(DeserializeArray(document.GetRoot(), output, pool, result, options) && result.UsedParallel, "threshold equality");
    options.MinElements = 0; options.MinTokens = 0;
    JobSystem one(1);
    Check(DeserializeArray(document.GetRoot(), output, one, result, options) && result.UsedParallel == false, "single worker sequential");
    for (const auto* text : {"[]", "[42]", "[3,2]"})
    {
        Check(JsonParser::Parse(text, document), "small fixture");
        std::vector<int> reference;
        Check(Deserialize(document.GetRoot(), reference), "reference small array");
        Check(DeserializeArray(document.GetRoot(), output, pool, result, options) && output == reference, "small array equals reference");
    }
    Check(JsonParser::Parse("[1,\"bad\",3,null,5,6,false]", document), "bad fixture");
    output = {99};
    for (bool enabled : {false, true})
    {
        options.EnableParallel = enabled;
        Check(DeserializeArray(document.GetRoot(), output, pool, result, options) == false && output == std::vector<int>{99}, "failure preserves output");
        Check(result.Status == ArrayReadStatus::ElementFailure && result.FailedElements == 3
            && result.FirstFailedIndex == 1 && std::string(result.FirstError.Path.data()) == "/1", "deterministic aggregate errors");
    }
    Check(JsonParser::Parse("{}", document), "nonarray fixture");
    Check(DeserializeArray(document.GetRoot(), output, pool, result, options) == false
        && result.Status == ArrayReadStatus::InvalidInput && output == std::vector<int>{99}, "nonarray preserves output");
    const std::string row = "{\"type\":\"Raven.StructuredDataTest\",\"version\":2,\"enabled\":true,\"label\":\"\\u65e5\\u672c\",\"values\":[1,2,3]}";
    Check(JsonParser::Parse("[" + row + "," + row + "," + row + "]", document), "typed escaped fixture");
    std::vector<TestSettings> typed, reference;
    Check(Deserialize(document.GetRoot(), reference), "typed sequential reference");
    Check(DeserializeArray(document.GetRoot(), typed, pool, result, options) && typed.size() == 3
        && typed[2].Label == reference[2].Label && typed[0].Values == reference[0].Values, "typed parallel escaped result");
    const auto retainedLabel = typed[0].Label;
    const std::string badRow = "{\"type\":\"Raven.StructuredDataTest\",\"version\":2,\"enabled\":true,\"values\":[1,null]}";
    Check(JsonParser::Parse("[" + row + "," + badRow + "," + badRow + "]", document), "nested error fixture");
    Check(DeserializeArray(document.GetRoot(), typed, pool, result, options) == false && result.FailedElements == 2
        && std::string(result.FirstError.Path.data()) == "/1/values/1", "nested diagnostic includes array index");
    Check(typed.size() == 3 && typed[0].Label == retainedLabel, "nested failure preserves owned output");
    std::atomic<bool> cancel{true};
    options.Cancel = &cancel;
    Check(DeserializeArray(document.GetRoot(), output, pool, result, options) == false
        && result.Status == ArrayReadStatus::Cancelled && result.SubmittedJobs == 0, "pre cancelled no jobs");
    Check(JsonParser::Parse("[1,2,3,4,5,6,7,8]", document), "cancel fixture");
    cancellationForTest = &cancel;
    std::vector<CancelValue> cancelOutput(1);
    cancelOutput[0].Value = 99;
    for (bool enabled : {false, true})
    {
        cancel = false; options.EnableParallel = enabled;
        Check(DeserializeArray(document.GetRoot(), cancelOutput, pool, result, options) == false
            && result.Status == ArrayReadStatus::Cancelled && cancelOutput.size() == 1 && cancelOutput[0].Value == 99, "mid read cancellation preserves output");
    }
    options.Cancel = nullptr;
    Check(JsonParser::Parse("[9,9,9,9]", document), "throw fixture");
    Check(DeserializeArray(document.GetRoot(), cancelOutput, pool, result, options) == false
        && result.FailedElements == 4 && cancelOutput[0].Value == 99, "throwing adapters collected");
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
    Check(JsonParser::Parse("[1,2,3,4,5,6,7,8]", document), "allocation fixture");
    bool completed = false;
    for (std::int64_t failure = 0; failure < 100; ++failure)
    {
        output = {99};
        BeginBenchmarkMemory();
        FailBenchmarkAllocationAfter(failure);
        const bool success = DeserializeArray(document.GetRoot(), output, pool, result, options);
        EndBenchmarkMemory();
        if (success) { completed = true; break; }
        Check(output == std::vector<int>{99} && result.Status == ArrayReadStatus::ResourceFailure, "allocation failure joins submitted jobs");
    }
    Check(completed, "allocation failure sweep completes");
#endif
    // 一度の失敗/取消の後も同じPoolを再利用できます。
    Check(JsonParser::Parse("[4,3,2,1]", document), "recovery fixture");
    Check(DeserializeArray(document.GetRoot(), output, pool, result, options) && output == std::vector<int>({4,3,2,1}), "pool reusable after failures");
    cancellationForTest = nullptr;
    std::cout << "ParallelDeserialize: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
} } }
