#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"
#include "Raven/Diagnostics/MeasurementTimeline.h"
#include "Raven/Diagnostics/BinaryTrace.h"
#include "Raven/Diagnostics/EventBuffer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace Raven::tests
{
namespace
{
using namespace Diagnostics;
void Require(bool condition, std::source_location location = std::source_location::current())
{
    if (condition == false)
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

void TestRecordingAndProfiler()
{
    std::vector<DiagnosticEvent> events;
    const auto drops = GetAsyncDroppedCount();
    std::uint64_t frame;
    SetMeasurementsEnabled(true);
    auto& profiler = CPUProfiler::Get();
    profiler.SetEnabled(true);
    {
        DiagnosticSession session(Capture(events));
        Require(session.IsActive());
        frame = BeginDiagnosticFrame();
        profiler.BeginFrame(frame);
        {
            ProfiledTraceScope outer("outer", DiagnosticCategory::Physics);
            {
                RAVEN_TRACE_SCOPE("child");
                RAVEN_COUNTER("candidates", 42.0);
            }
            profiler.AddCounter("legacy", 3.0);
            // Endも開始Frameを保持し、フレーム跨ぎの所要時間を分断しません。
            BeginDiagnosticFrame();
            SetMeasurementsEnabled(false);
            SetDiagnosticsEnabled(false);
        }
        SetDiagnosticsEnabled(true);
        SetMeasurementsEnabled(true);
        profiler.BeginFrame(GetDiagnosticFrameId());
        const auto& last = profiler.GetLastFrame();
        Require(last.FrameIndex == frame && last.Results.size() == 1);
        Require(last.Results[0].Name == "outer" && last.Results[0].DurationMilliseconds >= 0.0);
        Require(last.Counters.size() == 1 && last.Counters[0].Value == 3.0);

        std::vector<std::thread> workers;
        for (int worker = 0; worker < 4; ++worker)
        {
            workers.emplace_back([]()
            {
                for (int index = 0; index < 8; ++index)
                {
                    RAVEN_TRACE_SCOPE("worker");
                    RAVEN_COUNTER("work", static_cast<double>(index));
                }
            });
        }
        for (auto& worker : workers) { worker.join(); }
    }
    Require(GetAsyncDroppedCount() == drops);
    auto timeline = ReconstructMeasurementTimeline(events);
    Require(timeline.Intervals.size() == 34 && timeline.Counters.size() == 33);
    Require(timeline.MissingBegins == 0 && timeline.MissingEnds == 0 && timeline.MissingParents == 0);
    Require(timeline.InvalidEvents == 0);
    const auto outer = std::find_if(timeline.Intervals.begin(), timeline.Intervals.end(),
        [](const auto& interval) { return interval.Name == "outer"; });
    const auto child = std::find_if(timeline.Intervals.begin(), timeline.Intervals.end(),
        [](const auto& interval) { return interval.Name == "child"; });
    Require(outer != timeline.Intervals.end() && child != timeline.Intervals.end());
    Require(outer->FrameId == frame && outer->Complete);
    Require(child->ParentScopeId == outer->ScopeId && child->ThreadId == outer->ThreadId);
    Require(child->BeginNanoseconds >= outer->BeginNanoseconds && child->EndNanoseconds <= outer->EndNanoseconds);
    for (const auto& interval : timeline.Intervals) { Require(interval.Complete); }

    // CollectorのThread間の到着順を変えても、Scope IDによる再構築結果は変わりません。
    std::reverse(events.begin(), events.end());
    std::ostringstream saved;
    Require(WriteBinaryTraceHeader(saved));
    for (const auto& event : events) { Require(WriteBinaryTraceEvent(saved, event)); }
    std::istringstream input(saved.str());
    BinaryTraceReader reader(input);
    std::vector<DiagnosticEvent> reloaded;
    DiagnosticEvent event;
    while (reader.ReadNext(event) == TraceReadResult::Event) { reloaded.push_back(event); }
    Require(reader.ReadNext(event) == TraceReadResult::EndOfStream);
    auto restored = ReconstructMeasurementTimeline(reloaded);
    Require(restored.Intervals.size() == timeline.Intervals.size() && restored.Counters.size() == timeline.Counters.size());
    Require(restored.InvalidEvents == 0 && restored.MissingEnds == 0);
    for (std::size_t index = 0; index < restored.Intervals.size(); ++index)
    {
        Require(restored.Intervals[index].BeginNanoseconds == timeline.Intervals[index].BeginNanoseconds);
        Require(restored.Intervals[index].EndNanoseconds == timeline.Intervals[index].EndNanoseconds);
    }
}

void TestLossAndCompatibility()
{
    DiagnosticMetric metric;
    metric.Kind = DiagnosticMetricKind::TraceBegin;
    metric.ScopeId = 5;
    metric.FrameId = 9;
    metric.SetName("lost-end");
    DiagnosticEvent begin;
    begin.Id = EventIds::TraceBegin;
    begin.ThreadId = 7;
    begin.TimestampNanoseconds = 100;
    begin.Sequence = 1;
    begin.Payload = metric;
    DiagnosticFilter logsOnly;
    logsOnly.IncludeMeasurements = false;
    Require(logsOnly.Accepts(begin) == false);
    DiagnosticEvent logOnly;
    logOnly.Payload = std::int64_t(1);
    Require(logsOnly.Accepts(logOnly));
    EventBuffer buffer;
    Require(buffer.TryPush(begin));
    // 満杯でEndだけがDropされた状態を、スケジューラに依存せず再現します。
    for (std::size_t index = 1; index < EventBuffer::Capacity; ++index)
    {
        DiagnosticEvent log;
        log.Id = 1;
        Require(buffer.TryPush(log));
    }
    metric.Kind = DiagnosticMetricKind::TraceEnd;
    DiagnosticEvent end = begin;
    end.Id = EventIds::TraceEnd;
    end.Sequence = 2;
    end.TimestampNanoseconds = 200;
    end.Payload = metric;
    Require(buffer.TryPush(end) == false);
    std::vector<DiagnosticEvent> events;
    DiagnosticEvent event;
    while (buffer.TryPop(event)) { events.push_back(event); }
    auto loss = ReconstructMeasurementTimeline(events, buffer.GetDroppedCount());
    Require(loss.MissingEnds == 1 && loss.DroppedEvents == 1 && loss.Intervals[0].Complete == false);
    events = { end };
    loss = ReconstructMeasurementTimeline(events);
    Require(loss.MissingBegins == 1 && loss.Intervals[0].Complete == false);
    events = { begin, end, begin };
    Require(ReconstructMeasurementTimeline(events).InvalidEvents == 1);
    end.TimestampNanoseconds = 99;
    events = { begin, end };
    Require(ReconstructMeasurementTimeline(events).InvalidEvents == 1);
    Require(ReconstructMeasurementTimeline(events, 0, 1).InputLimitExceeded);

    metric.SetName(std::string(MaxDiagnosticMetricNameBytes - 1, 'a') + "é");
    Require(metric.NameBytes == MaxDiagnosticMetricNameBytes - 1);
    metric.Value = std::numeric_limits<double>::infinity();
    Require(metric.IsValid() == false);
    metric.Value = 0.0;
    metric.Kind = DiagnosticMetricKind::Counter;
    metric.ScopeId = 0;
    begin.Payload = metric;
    std::ostringstream saved;
    Require(WriteBinaryTraceHeader(saved) && WriteBinaryTraceEvent(saved, begin));
    const auto valid = saved.str();
    Require(valid[54] == 5 && valid[60] == 1 && valid[62] == 95);
    Require(valid[64] == 9 && valid[72] == 0 && valid[80] == 0 && valid[96] == 'a');
    for (const std::size_t offset : { 60u, 61u, 62u })
    {
        auto corrupt = valid;
        corrupt[offset] = static_cast<char>(255);
        std::istringstream input(corrupt);
        BinaryTraceReader reader(input);
        Require(reader.ReadNext(event) == TraceReadResult::InvalidData);
    }
    auto infinite = valid;
    infinite[94] = static_cast<char>(0xf0);
    infinite[95] = static_cast<char>(0x7f);
    std::istringstream infiniteInput(infinite);
    BinaryTraceReader infiniteReader(infiniteInput);
    Require(infiniteReader.ReadNext(event) == TraceReadResult::InvalidData);
    for (std::size_t length = 17; length < valid.size(); ++length)
    {
        std::istringstream truncated(valid.substr(0, length));
        BinaryTraceReader truncatedReader(truncated);
        Require(truncatedReader.ReadNext(event) == TraceReadResult::InvalidData);
    }
    auto oldHeaderMetric = valid;
    oldHeaderMetric[10] = 0;
    std::istringstream oldMetricInput(oldHeaderMetric);
    BinaryTraceReader oldMetricReader(oldMetricInput);
    Require(oldMetricReader.ReadNext(event) == TraceReadResult::InvalidData);
    // v1.0のLogは新Readerでも読み込みます。LogのRecord配置は変更していません。
    begin.Payload = std::int64_t(-7);
    std::ostringstream log;
    Require(WriteBinaryTraceHeader(log) && WriteBinaryTraceEvent(log, begin));
    auto oldLog = log.str();
    oldLog[10] = 0;
    std::istringstream oldInput(oldLog);
    BinaryTraceReader oldReader(oldInput);
    Require(oldReader.ReadNext(event) == TraceReadResult::Event && std::get<std::int64_t>(event.Payload) == -7);
}

void TestSessionBoundaries()
{
    SetMeasurementsEnabled(true);
    std::vector<DiagnosticEvent> first, second;
    Require(StartAsyncLogging(Capture(first)));
    auto oldScope = std::make_unique<TraceScope>("old-session");
    StopAsyncLogging();
    Require(StartAsyncLogging(Capture(second)));
    {
        TraceScope current("new-session");
        oldScope.reset();
    }
    StopAsyncLogging();
    Require(first.size() == 1 && second.size() == 2);
    Require(std::get<DiagnosticMetric>(second[0].Payload).ParentScopeId == 0);
    Require(ReconstructMeasurementTimeline(first).MissingEnds == 1);
    Require(ReconstructMeasurementTimeline(second).Intervals[0].Complete);
    SetMeasurementsEnabled(false);
    int sideEffect = 0;
    RAVEN_COUNTER("disabled", ++sideEffect);
    RAVEN_TRACE_SCOPE((++sideEffect, "disabled"));
    Require(sideEffect == 0);
}

class NullSink final : public IDiagnosticSink
{
public:
    bool Write(const DiagnosticEvent&) override { return true; }
    bool Flush() override { return true; }
};

void MeasureOverhead()
{
    std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
    sinks.push_back(std::make_unique<NullSink>());
    DiagnosticSession session(std::move(sinks));
    Require(session.IsActive());
    SetMeasurementsEnabled(true);
    // 初回TLS登録を定常発行の測定から除外します。各batchはBuffer容量未満に制限します。
    RAVEN_COUNTER("warmup", 0.0);
    FlushDiagnostics();
    const auto droppedBefore = GetAsyncDroppedCount();
    constexpr int Batches = 64;
    constexpr int PerBatch = 64;
    double counterNs = 0.0, scopeNs = 0.0;
    for (int batch = 0; batch < Batches; ++batch)
    {
        auto start = std::chrono::steady_clock::now();
        for (int index = 0; index < PerBatch; ++index) { RAVEN_COUNTER("bench", index); }
        auto end = std::chrono::steady_clock::now();
        counterNs += std::chrono::duration<double, std::nano>(end - start).count();
        FlushDiagnostics();
        start = std::chrono::steady_clock::now();
        for (int index = 0; index < PerBatch; ++index) { RAVEN_TRACE_SCOPE("bench"); }
        end = std::chrono::steady_clock::now();
        scopeNs += std::chrono::duration<double, std::nano>(end - start).count();
        FlushDiagnostics();
    }
    Require(GetAsyncDroppedCount() == droppedBefore);
    SetMeasurementsEnabled(false);
    int sideEffect = 0;
    auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < Batches * PerBatch; ++index)
    {
        RAVEN_COUNTER("bench", ++sideEffect);
        RAVEN_TRACE_SCOPE((++sideEffect, "bench"));
    }
    auto end = std::chrono::steady_clock::now();
    Require(sideEffect == 0);
    constexpr double Calls = Batches * PerBatch;
    std::cout << "measurement_counter_ns=" << counterNs / Calls << " trace_scope_ns=" << scopeNs / Calls
        << " runtime_disabled_counter_and_scope_ns="
        << std::chrono::duration<double, std::nano>(end - start).count() / Calls << " drops=0\n";
}
}

void RunDiagnosticMeasurementSelfTests()
{
    TestRecordingAndProfiler();
    TestLossAndCompatibility();
    TestSessionBoundaries();
    MeasureOverhead();
}
}
