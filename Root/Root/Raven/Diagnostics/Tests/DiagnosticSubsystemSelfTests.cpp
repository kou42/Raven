#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"
#include "Raven/Diagnostics/BinaryTrace.h"
#include "Raven/Diagnostics/MeasurementTimeline.h"
#include "Raven/Renderer/RHI/RHIResource.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace Raven::tests
{
namespace
{
using namespace Diagnostics;
void Require(bool value, std::source_location location = std::source_location::current())
{
    if (value == false)
    {
        throw std::runtime_error(std::string(location.file_name()) + ":" + std::to_string(location.line()));
    }
}
class CaptureSink final : public IDiagnosticSink
{
public:
    explicit CaptureSink(std::vector<DiagnosticEvent>& events) : m_Events(events) {}
    bool Write(const DiagnosticEvent& event) override { m_Events.push_back(event); return true; }
    bool Flush() override { return true; }
private:
    std::vector<DiagnosticEvent>& m_Events;
};
std::vector<std::unique_ptr<IDiagnosticSink>> Capture(std::vector<DiagnosticEvent>& events)
{
    std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
    sinks.push_back(std::make_unique<CaptureSink>(events));
    return sinks;
}

void TestValidationConversion()
{
    std::vector<DiagnosticEvent> events;
    const auto frame = BeginDiagnosticFrame();
    {
        DiagnosticSession session(Capture(events));
        Require(session.IsActive());
        // 通常EmitでTLS登録を完了させ、登録mutexの競合による意図したfallbackと分離します。
        RAVEN_LOG_INFO(DiagnosticCategory::Runtime, 1, std::int64_t(0));
        FlushDiagnostics();
        std::string source = "owned validation";
        const std::array<std::uint32_t, 4> levels{ 0x1000u, 0x100u, 0x10u, 1u };
        for (const auto level : levels) { ReportVulkanValidation(level, 2, -42, "VUID-test", source); }
        for (std::uint32_t level = 0; level <= 4; ++level) { ReportDX12Validation(level, 777, source); }
        ReportVulkanValidation(0x100, 4, 9, std::string(127, 'a') + "é", std::string(1023, 'b') + "é");
        source.assign("changed");
    }
    Require(events.size() == 11);
    for (std::size_t index = 1; index <= 9; ++index)
    {
        const auto& validation = std::get<DiagnosticValidation>(events[index].Payload);
        Require(events[index].Category == DiagnosticCategory::RHI && validation.Message == "owned validation");
        Require(validation.FrameId == frame);
        if (index <= 4)
        {
            Require(events[index].Id == EventIds::VulkanValidation && validation.MessageId == -42);
            Require(validation.Name == "VUID-test" && validation.NativeType == 2);
            Require(events[index].Severity == (index == 1 ? DiagnosticSeverity::Error
                : (index == 2 ? DiagnosticSeverity::Warning : DiagnosticSeverity::Info)));
        }
        else
        {
            Require(events[index].Id == EventIds::DX12Validation && validation.MessageId == 777);
            Require(events[index].Severity == (index <= 6 ? DiagnosticSeverity::Error
                : (index == 7 ? DiagnosticSeverity::Warning : DiagnosticSeverity::Info)));
            Require(validation.NativeSeverity == index - 5);
        }
    }
    const auto& limited = std::get<DiagnosticValidation>(events.back().Payload);
    Require(limited.Name.size() == 127 && limited.Message.size() == 1023);
    Require(limited.NameTruncated && limited.MessageTruncated);
    Require(BoundedValidationString(nullptr, 10).empty());
    Require(BoundedValidationString("abc", 2) == "ab");
    const auto fallbackBefore = GetValidationFallbackCount();
    ReportDX12Validation(1, 5, "shutdown fallback (expected)");
    Require(GetValidationFallbackCount() == fallbackBefore + 1);

    std::ostringstream saved;
    Require(WriteBinaryTraceHeader(saved));
    for (const auto& event : events) { Require(WriteBinaryTraceEvent(saved, event)); }
    std::istringstream input(saved.str());
    BinaryTraceReader reader(input);
    DiagnosticEvent decoded;
    for (const auto& event : events)
    {
        Require(reader.ReadNext(decoded) == TraceReadResult::Event);
        Require(decoded.Payload == event.Payload && decoded.Id == event.Id && decoded.Severity == event.Severity);
    }
    Require(reader.ReadNext(decoded) == TraceReadResult::EndOfStream);
}

void TestCategoryControl()
{
    std::vector<DiagnosticEvent> events;
    SetMeasurementsEnabled(true);
    {
        DiagnosticSession session(Capture(events));
        Require(session.IsActive());
        SetDiagnosticCategoryEnabled(DiagnosticCategory::Physics, false);
        int evaluated = 0;
        RAVEN_LOG_INFO(DiagnosticCategory::Physics, 1, std::int64_t(++evaluated));
        RecordCounter("disabled", 1, DiagnosticCategory::Physics);
        { TraceScope disabled("disabled", DiagnosticCategory::Physics); }
        Require(evaluated == 0);
        // SoftBodyの有効設定はPhysicsの無効設定と独立します。
        RecordCounter("softbody", 2, DiagnosticCategory::SoftBody);
        RecordCounter("renderer", 3, DiagnosticCategory::Renderer);
        SetDiagnosticCategoryEnabled(DiagnosticCategory::Physics, true);
        {
            TraceScope scope("close-after-disable", DiagnosticCategory::Physics);
            SetDiagnosticCategoryEnabled(DiagnosticCategory::Physics, false);
        }
        SetDiagnosticCategoryEnabled(DiagnosticCategory::Physics, true);
        FlushDiagnostics();
        const auto before = events.size();
        SetDiagnosticCategoryEnabled(DiagnosticCategory::RHI, false);
        const auto fallback = GetValidationFallbackCount();
        ReportVulkanValidation(0x1000, 2, 4, {}, "disabled capture fallback (expected)");
        Require(GetValidationFallbackCount() == fallback + 1);
        FlushDiagnostics();
        Require(events.size() == before);
        SetDiagnosticCategoryEnabled(DiagnosticCategory::RHI, true);
    }
    const auto timeline = ReconstructMeasurementTimeline(events);
    Require(timeline.Counters.size() == 2 && timeline.Intervals.size() == 1 && timeline.Intervals[0].Complete);
    SetMeasurementsEnabled(false);
}

void TestValidationFormat()
{
    DiagnosticValidation validation;
    validation.MessageId = -42;
    validation.NativeSeverity = 0x1000;
    validation.NativeType = 2;
    validation.FrameId = 9;
    validation.Name = std::string(MaxValidationNameBytes, 'N');
    validation.Message = std::string(MaxValidationMessageBytes, 'M');
    DiagnosticEvent event;
    event.Id = EventIds::VulkanValidation;
    event.Category = DiagnosticCategory::RHI;
    event.Severity = DiagnosticSeverity::Error;
    event.Payload = validation;
    std::ostringstream saved;
    Require(WriteBinaryTraceHeader(saved) && WriteBinaryTraceEvent(saved, event));
    const auto bytes = saved.str();
    Require(bytes[10] == 2 && bytes[54] == 6 && bytes[60] == 1);
    Require(static_cast<unsigned char>(bytes[68]) == 0xd6 && static_cast<unsigned char>(bytes[75]) == 0xff);
    Require(bytes[77] == 0x10 && bytes[80] == 2 && bytes[84] == 9 && bytes[92] == 'N');
    std::istringstream input(bytes);
    BinaryTraceReader reader(input);
    DiagnosticEvent restored;
    Require(reader.ReadNext(restored) == TraceReadResult::Event && restored.Payload == event.Payload);
    for (std::size_t length = 17; length < bytes.size(); ++length)
    {
        std::istringstream truncated(bytes.substr(0, length));
        BinaryTraceReader truncatedReader(truncated);
        restored.Id = 77;
        Require(truncatedReader.ReadNext(restored) == TraceReadResult::InvalidData && restored.Id == 77);
    }
    for (const std::size_t offset : { 60u, 61u, 62u, 65u, 66u })
    {
        auto invalid = bytes;
        invalid[offset] = static_cast<char>(255);
        std::istringstream corrupted(invalid);
        BinaryTraceReader corruptedReader(corrupted);
        Require(corruptedReader.ReadNext(restored) == TraceReadResult::InvalidData);
    }
    auto old = bytes;
    old[10] = 1;
    std::istringstream oldValidation(old);
    BinaryTraceReader oldReader(oldValidation);
    Require(oldReader.ReadNext(restored) == TraceReadResult::InvalidData);
    validation.Message.push_back('x');
    event.Payload = validation;
    std::ostringstream rejected;
    Require(WriteBinaryTraceEvent(rejected, event) == false && rejected.str().empty());
    // SoftBody categoryはv1.2からです。古いHeaderに混入した場合は拒否します。
    event.Payload = std::int64_t(1);
    event.Category = DiagnosticCategory::SoftBody;
    std::ostringstream softbody;
    Require(WriteBinaryTraceHeader(softbody) && WriteBinaryTraceEvent(softbody, event));
    auto legacyCategory = softbody.str();
    legacyCategory[10] = 1;
    std::istringstream legacyInput(legacyCategory);
    BinaryTraceReader legacyReader(legacyInput);
    Require(legacyReader.ReadNext(restored) == TraceReadResult::InvalidData);
}

class CallbackSink final : public IDiagnosticSink
{
public:
    bool Write(const DiagnosticEvent&) override { return true; }
    bool Flush() override
    {
        // WorkerのFlush中は、呼出側のFlushDiagnosticsが制御mutexを保持しています。
        // driverから同期的に再入した場合も、そのmutexを待たずにstderrへ退避させます。
        ReportDX12Validation(1, 11, "callback during flush fallback (expected)");
        return true;
    }
};

void TestCallbackDuringFlush()
{
    std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
    sinks.push_back(std::make_unique<CallbackSink>());
    Require(StartAsyncLogging(std::move(sinks)));
    const auto before = GetValidationFallbackCount();
    auto flush = std::async(std::launch::async, [] { FlushDiagnostics(); });
    if (flush.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
    {
        // future destructorの待機で検証process自体が止まらないよう、deadlockを終了コードへ変換します。
        std::fputs("Validation callback deadlocked during Flush\n", stderr);
        std::_Exit(1);
    }
    flush.get();
    StopAsyncLogging();
    Require(GetValidationFallbackCount() >= before + 1);
}
class TestResource final : public RHIResource {};
void TestResourceLifetime()
{
    const auto before = RHIResource::GetLiveWrapperCount();
    {
        auto resource = std::make_shared<TestResource>();
        auto borrowedOwner = resource;
        Require(RHIResource::GetLiveWrapperCount() == before + 1);
        resource.reset();
        Require(RHIResource::GetLiveWrapperCount() == before + 1);
    }
    Require(RHIResource::GetLiveWrapperCount() == before);
    std::vector<std::thread> threads;
    for (int index = 0; index < 4; ++index)
    {
        threads.emplace_back([] { for (int count = 0; count < 1000; ++count) { TestResource resource; } });
    }
    for (auto& thread : threads) { thread.join(); }
    Require(RHIResource::GetLiveWrapperCount() == before);
}
}

void RunDiagnosticSubsystemSelfTests()
{
    TestValidationConversion();
    TestCategoryControl();
    TestValidationFormat();
    TestCallbackDuringFlush();
    TestResourceLifetime();
}
}
