#include "Raven/Data/Tests/MutableDataDocumentTests.h"
#include "Raven/Data/MutableDataDocument.h"
#include "Raven/Data/Formats/Json/JsonParser.h"
#include "Raven/Data/Formats/Json/JsonWriter.h"
#include "Raven/Data/Formats/MessagePack/MessagePackParser.h"
#include "Raven/Data/Formats/MessagePack/MessagePackWriter.h"
#include "Raven/Data/Tests/StructuredDataTestAsset.h"
#include "Raven/Data/Tests/DataBenchmarkMemory.h"
#include <future>
#include <iostream>
#include <limits>
#include <new>

namespace Raven { namespace Data { namespace Tests {
namespace {
int checks = 0, failures = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (condition == false) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::string Bytes(const MutableDataDocument& document)
{
    DataDocument frozen;
    std::string output;
    Check(document.Freeze(frozen) && MessagePackWriter::Write(frozen, output), "freeze canonical bytes");
    return output;
}
}
int RunMutableDataDocumentTests()
{
    checks = 0; failures = 0;
    DataDocument input, value, frozen;
    Check(JsonParser::Parse("{\"label\":\"\\u65e5\\u672c\",\"a\":[1,{\"x\":true},3],\"empty\":null,\"large\":18446744073709551615,\"float\":-0.0}", input), "input fixture");
    Check(JsonParser::Parse("42", value), "value fixture");
    MutableDataDocument edit;
    DataError error;
    Check(edit.Freeze(frozen, &error) == false && error.Code == DataErrorCode::InvalidStructure,
        "uninitialized document freeze rejected");
    Check(edit.Reset(input.GetRoot(), &error), "immutable to mutable");
    std::string canonical;
    Check(MessagePackWriter::Write(input, canonical) && Bytes(edit) == canonical, "all JSON types exact freeze");
    const auto root = edit.GetRoot();
    MutableDataHandle array, label, nested, descendant, sibling, inserted;
    Check(edit.TryFind(root, "a", array) && edit.TryFind(root, "label", label), "find decoded members");
    std::string_view view;
    Check(edit.TryGetStringView(label, view) && view == "\xe6\x97\xa5\xe6\x9c\xac", "owned decoded view");
    Check(edit.TryGetChild(array, 1, nested) && edit.TryFind(nested, "x", descendant)
        && edit.TryGetChild(array, 2, sibling), "nested handles");
    Check(edit.Remove(nested), "remove subtree");
    Check(edit.IsValid(nested) == false && edit.IsValid(descendant) == false
        && edit.IsValid(sibling) && edit.IsValid(array), "delete invalidates subtree only");
    Check(edit.InsertArray(array, 1, value.GetRoot(), inserted), "reuse deleted slot");
    Check(inserted.Slot.index == nested.Slot.index && inserted.Slot.generation != nested.Slot.generation
        && edit.IsValid(nested) == false, "same slot new generation");
    Check(edit.Freeze(inserted, frozen) && frozen.GetRoot().IsType(DataType::UInt64), "freeze subtree");
    auto unchanged = inserted;
    const auto beforeBad = Bytes(edit);
    const auto revision = edit.GetRevision();
    Check(edit.TryGetStringView(label, view), "reacquire view");
    const auto address = view.data();
    Check(edit.InsertObject(root, "a", value.GetRoot(), unchanged, &error) == false
        && error.Code == DataErrorCode::DuplicateKey && unchanged == inserted, "duplicate decoded key rejection");
    Check(Bytes(edit) == beforeBad && edit.GetRevision() == revision && view.data() == address
        && view == "\xe6\x97\xa5\xe6\x9c\xac", "failed edit preserves state handle view revision");
    Check(edit.InsertObject(root, std::string_view("\xff", 1), value.GetRoot(), unchanged, &error) == false
        && error.Code == DataErrorCode::InvalidEncoding, "invalid UTF-8 key rejected");
    Check(edit.InsertArray(array, 999, value.GetRoot(), unchanged) == false && Bytes(edit) == beforeBad, "bad array index preserves state");
    Check(edit.InsertObject(root, std::string_view("k\0x", 3), value.GetRoot(), inserted)
        && edit.TryFind(root, std::string_view("k\0x", 3), unchanged) && unchanged == inserted, "NUL key preserved");
    Check(edit.Replace(label, value.GetRoot(), inserted) && edit.IsValid(label) == false
        && edit.IsValid(root) && edit.IsValid(sibling), "replacement invalidates target only");
    Check(edit.InsertArray(array, 0, value.GetRoot(), inserted), "array front insertion");
    std::uint32_t count = 0;
    Check(edit.TryGetCount(array, count) && edit.InsertArray(array, count, value.GetRoot(), inserted), "array end insertion");
    Check(edit.Remove(root, &error) == false, "root removal rejected");
    MutableDataDocument other;
    Check(other.Reset(input.GetRoot()) && other.IsValid(root) == false && edit.IsValid(other.GetRoot()) == false, "cross document handles rejected");
    const auto sparseSlots = edit.GetSlotCount();
    Check(edit.TryGetChild(array, 0, inserted) && edit.Remove(inserted), "make sparse slots");
    const auto beforeCompact = Bytes(edit);
    Check(edit.Compact() && Bytes(edit) == beforeCompact && edit.GetSlotCount() < sparseSlots
        && edit.IsValid(root) == false, "compact preserves meaning invalidates all handles");
    const auto beforeReset = edit.GetRoot();
    Check(edit.Reset(input.GetRoot()) && edit.IsValid(beforeReset) == false, "reset invalidates epoch");
    Check(edit.Freeze(frozen), "independent snapshot");
    auto reader = std::async(std::launch::async, [&frozen]()
    {
        for (int i = 0; i < 100; ++i)
        {
            DataElement field;
            std::uint64_t number;
            if (frozen.GetRoot().TryFind("large", field) == false || field.TryGetUInt64(number) == false
                || number != (std::numeric_limits<std::uint64_t>::max)()) { return false; }
        }
        return true;
    });
    Check(edit.Replace(edit.GetRoot(), value.GetRoot(), inserted), "replace root");
    Check(reader.get() && edit.IsValid(beforeReset) == false, "frozen worker unaffected by edits");
    Check(edit.Reset({}, &error) == false && edit.IsValid(inserted), "invalid reset preserves root");
    // Documentを同じアドレスへ再構築してもOwner IDは再利用しません。
    alignas(MutableDataDocument) std::byte storage[sizeof(MutableDataDocument)];
    auto* reused = new (storage) MutableDataDocument();
    Check(reused->Reset(value.GetRoot()), "same address first owner");
    const auto deadOwner = reused->GetRoot();
    reused->~MutableDataDocument();
    reused = new (storage) MutableDataDocument();
    Check(reused->Reset(value.GetRoot()) && reused->IsValid(deadOwner) == false, "same address distinct owner");
    reused->~MutableDataDocument();
    const std::string binary("\x82\xa1" "s" "\xa2" "\\x" "\xa1" "b" "\xc4\x03\0\xff\x01", 13);
    // String中のliteral backslashとBinaryをJSONを介さず保存します。
    Check(MessagePackParser::Parse(binary, input), "binary fixture");
    Check(edit.Reset(input.GetRoot()) && MessagePackWriter::Write(input, canonical) && Bytes(edit) == canonical, "binary and literal backslash freeze");
    DataLimits limits;
    limits.MaxDepth = 2; limits.MaxTokens = 4; limits.MaxValueBytes = 8;
    MutableDataDocument limited(limits);
    Check(JsonParser::Parse("[[]]", input) && limited.Reset(input.GetRoot()), "limit fixture");
    const auto limitedRoot = limited.GetRoot();
    Check(limited.TryGetChild(limitedRoot, 0, nested), "nested limit parent");
    Check(JsonParser::Parse("[]", value), "depth insertion fixture");
    Check(limited.InsertArray(nested, 0, value.GetRoot(), inserted, &error) == false
        && error.Code == DataErrorCode::LimitExceeded && limited.IsValid(nested), "combined depth limit before commit");
    Check(JsonParser::Parse("[1,2,3]", input) && limited.Reset(input.GetRoot()), "exact token limit");
    Check(JsonParser::Parse("4", value), "token insertion fixture");
    Check(limited.InsertArray(limited.GetRoot(), 3, value.GetRoot(), inserted, &error) == false
        && error.Code == DataErrorCode::LimitExceeded, "one token over limit");
    DataLimits byteLimits;
    byteLimits.MaxInputBytes = 4;
    byteLimits.MaxValueBytes = 4;
    MutableDataDocument byteLimited(byteLimits);
    Check(JsonParser::Parse("{\"a\":\"123\"}", input) && byteLimited.Reset(input.GetRoot()), "exact total decoded bytes");
    Check(JsonParser::Parse("\"x\"", value), "byte insertion fixture");
    Check(byteLimited.InsertObject(byteLimited.GetRoot(), "b", value.GetRoot(), inserted, &error) == false
        && error.Code == DataErrorCode::LimitExceeded, "aggregate bytes checked before allocation");
    Check(byteLimited.TryFind(byteLimited.GetRoot(), "a", label) && byteLimited.Remove(label)
        && byteLimited.InsertObject(byteLimited.GetRoot(), "b", value.GetRoot(), inserted), "deletion releases key and value budget");
    DataLimits keyLimits;
    keyLimits.MaxTokens = 3;
    MutableDataDocument keyLimited(keyLimits);
    Check(JsonParser::Parse("{\"a\":1}", input) && keyLimited.Reset(input.GetRoot()), "exact object token limit counts key");
    Check(keyLimited.InsertObject(keyLimited.GetRoot(), "b", value.GetRoot(), inserted, &error) == false
        && error.Code == DataErrorCode::LimitExceeded, "object key token budget enforced");
    DataLimits containerLimits;
    containerLimits.MaxContainerElements = 1;
    MutableDataDocument containerLimited(containerLimits);
    Check(JsonParser::Parse("[1]", input) && containerLimited.Reset(input.GetRoot())
        && containerLimited.InsertArray(containerLimited.GetRoot(), 1, value.GetRoot(), inserted, &error) == false
        && error.Code == DataErrorCode::LimitExceeded, "container count limit");
    DataLimits extended;
    extended.MaxDepth = MutableDataMaxDepth + 1;
    Check(JsonParser::Parse(std::string(MutableDataMaxDepth + 1, '[') + "0" + std::string(MutableDataMaxDepth + 1, ']'), input, &error, extended), "deep immutable fixture");
    MutableDataDocument bounded(extended);
    Check(bounded.Reset(input.GetRoot(), &error) == false && error.Code == DataErrorCode::LimitExceeded,
        "mutable recursion hard depth cap");
    DataLimits invalidLimits;
    invalidLimits.MaxTokens = InvalidTokenId;
    MutableDataDocument invalid(invalidLimits);
    Check(invalid.Reset(value.GetRoot(), &error) == false && error.Code == DataErrorCode::InvalidLimits,
        "invalid limits rejected before import");
    // 専用設定を編集してFreezeし、既存の安全な保存経路へ渡します。
    Check(JsonParser::Parse("{\"type\":\"Raven.StructuredDataTest\",\"version\":2,\"enabled\":true,\"label\":\"before\",\"values\":[1,2]}", input), "save fixture");
    Check(edit.Reset(input.GetRoot()) && edit.TryFind(edit.GetRoot(), "label", label), "save edit target");
    Check(JsonParser::Parse("\"after\"", value) && edit.Replace(label, value.GetRoot(), inserted) && edit.Freeze(frozen), "edit and freeze settings");
    TestSettings settings, restored;
    Check(Deserialize(frozen.GetRoot(), settings), "typed conversion of edited settings");
    std::filesystem::path directory;
    for (unsigned i = 0; i < 128; ++i)
    {
        auto candidate = std::filesystem::temp_directory_path() / ("raven-mutable-test-" + std::to_string(i));
        if (std::filesystem::create_directory(candidate)) { directory = candidate; break; }
    }
    Check(directory.empty() == false, "isolated save directory");
    if (directory.empty() == false)
    {
        TestAssetError fileError;
        const auto path = directory / "edited.json";
        Check(SaveTestAsset(path, settings, fileError) && LoadTestAsset(path, restored, fileError)
            && restored.Label == "after" && restored.Values == std::vector<int>({1,2}), "edited asset disk round trip");
        std::filesystem::remove(path); std::filesystem::remove(directory);
    }
#ifdef RAVEN_DATA_BENCHMARK_MEMORY
    Check(JsonParser::Parse("[1,{\"a\":2},3]", input) && JsonParser::Parse("{\"b\":[4,5]}", value), "allocation fixtures");
    // Reset/Insert/Remove/Replace/Compact/Freezeの各確保点で停止し、元の状態と出力を検査します。
    for (unsigned operation = 0; operation < 6; ++operation)
    {
        bool completed = false;
        for (std::int64_t failure = 0; failure < 500; ++failure)
        {
            Check(edit.Reset(input.GetRoot()) && edit.TryGetChild(edit.GetRoot(), 1, nested), "prepare allocation sweep");
            const auto baseline = Bytes(edit);
            const auto originalRoot = edit.GetRoot();
            const auto originalRevision = edit.GetRevision();
            unchanged = originalRoot;
            DataDocument output;
            Check(JsonParser::Parse("99", output), "preserved freeze output");
            BeginBenchmarkMemory(); FailBenchmarkAllocationAfter(failure);
            bool success = false;
            switch (operation)
            {
            case 0: success = edit.Reset(value.GetRoot(), &error); break;
            case 1: success = edit.InsertArray(originalRoot, 1, value.GetRoot(), unchanged, &error); break;
            case 2: success = edit.Remove(nested, &error); break;
            case 3: success = edit.Replace(nested, value.GetRoot(), unchanged, &error); break;
            case 4: success = edit.Compact(&error); break;
            default: success = edit.Freeze(output, &error); break;
            }
            EndBenchmarkMemory();
            if (success) { completed = true; break; }
            std::uint64_t retained = 0;
            Check(error.Code == DataErrorCode::OutOfMemory && Bytes(edit) == baseline
                && edit.IsValid(originalRoot) && edit.GetRevision() == originalRevision && unchanged == originalRoot
                && output.GetRoot().TryGetUInt64(retained) && retained == 99, "allocation failure preserves state and outputs");
        }
        Check(completed, "all mutable allocation sites injected");
    }
#endif
    std::cout << "MutableDataDocument: " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
} } }
