#include "Raven/Diagnostics/Tests/DiagnosticSelfTests.h"
#include "Raven/Editor/Panels/DiagnosticsViewModel.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace Raven::tests
{
namespace
{
void CheckViewer(bool condition, std::source_location where = std::source_location::current())
{
    if (condition == false) { throw std::runtime_error(std::string(where.file_name()) + ':' + std::to_string(where.line())); }
}
}
void RunDiagnosticViewerSelfTests()
{
    using namespace Diagnostics;
    auto collected = std::make_shared<DiagnosticEventStore>();
    {
        std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
        sinks.push_back(std::make_unique<DiagnosticStoreSink>(collected));
        DiagnosticSession session(std::move(sinks));
        CheckViewer(session.IsActive());
        Emit(1, DiagnosticCategory::RHI, DiagnosticSeverity::Warning, std::string("collector to editor"));
        FlushDiagnostics();
        CheckViewer(collected->TrySnapshot(0)->Events.size() == 1);
    }
    CheckViewer(collected->TrySnapshot(0)->Events.size() == 1);
    DiagnosticEvent event; event.Id = 1; event.ThreadId = 42; event.Payload = std::string("owned message");
    DiagnosticEventStore small({3, 8192});
    small.Append(event);
    auto first = small.TrySnapshot(0);
    CheckViewer(first != nullptr && first->Events.size() == 1);
    const auto selectedId = first->Events.front().Id;
    DiagnosticsViewModel model; model.SetSnapshot(first); model.SelectRow(0);
    event.Payload = std::string("changed"); small.Append(event);
    model.SetSnapshot(small.TrySnapshot(0));
    CheckViewer(model.GetSelectedEvent() != nullptr && model.GetSelectedEvent()->Id == selectedId);
    auto filter = model.GetFilter(); filter.Search = "absent"; model.SetFilter(filter);
    CheckViewer(model.GetRows().empty() && model.GetSelectedRow().has_value() == false && model.GetSelectedEvent() != nullptr);
    small.Append(event); small.Append(event); model.SetSnapshot(small.TrySnapshot(0));
    CheckViewer(model.WasSelectionEvicted() && model.GetSelectedEvent() == nullptr);
    CheckViewer(std::get<std::string>(first->Events.front().Event.Payload) == "owned message");
    small.Clear(); small.Append(event); auto cleared = small.TrySnapshot(0);
    CheckViewer(cleared->Events.front().Id > selectedId && cleared->Evicted == 0);
    event.Payload = std::string(MaxDiagnosticTextBytes + 1, 'x'); small.Append(event);
    CheckViewer(small.TrySnapshot(0)->Rejected == 1);
    // 件数上限より前でも、可変長本文がbyte予算を使い切れば古い行を破棄します。
    const auto fixedBytes = sizeof(DiagnosticStoredEvent) * 4;
    DiagnosticEventStore bytes({4, fixedBytes + 1500});
    event.Payload = std::string(1000, 'x');
    bytes.Append(event); bytes.Append(event);
    const auto bounded = bytes.TrySnapshot(0);
    CheckViewer(bounded->Events.size() == 1 && bounded->Evicted == 1 && bounded->RetainedBytes <= fixedBytes + 1500);

    DiagnosticEventStore store({1024, 1024 * 1024});
    event.Payload = std::string("unassociated"); event.Category = DiagnosticCategory::Core; store.Append(event);
    DiagnosticValidation validation; validation.FrameId = 9; validation.Message = "device error";
    event.Payload = validation; event.Category = DiagnosticCategory::RHI; event.Severity = DiagnosticSeverity::Error; store.Append(event);
    DiagnosticMetric metric; metric.SetName("Physics.Step"); metric.FrameId = 9; metric.ScopeId = 7; metric.Kind = DiagnosticMetricKind::TraceBegin;
    event.Category = DiagnosticCategory::Physics; event.Severity = DiagnosticSeverity::Info; event.TimestampNanoseconds = 10; event.Payload = metric; store.Append(event);
    metric.Kind = DiagnosticMetricKind::TraceEnd; event.Sequence = 1; event.TimestampNanoseconds = 30; event.Payload = metric; store.Append(event);
    DiagnosticsViewModel view; view.SetSnapshot(store.TrySnapshot(0), 3, 4);
    filter = {}; filter.Frame = 9; filter.MinimumSeverity = DiagnosticSeverity::Error; filter.Search = "device"; view.SetFilter(filter);
    CheckViewer(view.GetRows().size() == 1 && view.GetTimeline().Intervals.size() == 1);
    CheckViewer(view.GetTimeline().Intervals.front().Complete && view.GetTimeline().DroppedEvents == 3 && view.GetNativeFallbacks() == 4);
    filter = {}; filter.UnassociatedOnly = true; view.SetFilter(filter);
    CheckViewer(view.GetRows().size() == 1 && view.GetTimeline().Intervals.empty());
    metric.Kind = DiagnosticMetricKind::Counter; metric.ScopeId = 0; metric.SetName("Counter");
    for (std::uint64_t i = 0; i < 600; ++i)
    {
        metric.FrameId = i; metric.Value = static_cast<double>(i); event.TimestampNanoseconds = i; event.Payload = metric; store.Append(event);
    }
    view.SetFilter({}); view.SetSnapshot(store.TrySnapshot(0));
    CheckViewer(view.GetCounterGraph().size() == 512 && view.GetCounterGraph().front().Value == 88);

    // Snapshotを保持したままCollector相当のWriterを動かしても参照先は変化しません。
    std::atomic<bool> done = false;
    std::jthread writer([&]() { for (int i = 0; i < 20000; ++i) { store.Append(event); } done.store(true); });
    while (done.load() == false)
    {
        const auto snapshot = store.TrySnapshot(0);
        if (snapshot != nullptr) { CheckViewer(snapshot->Events.size() <= 1024 && snapshot->RetainedBytes <= 1024 * 1024); }
    }
    writer.join();
    DiagnosticEventStore production;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) { production.Append(event); }
    auto snapshot = production.TrySnapshot(0); view.SetSnapshot(snapshot);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    CheckViewer(snapshot->Evicted > 0 && snapshot->Events.size() <= 8192 && snapshot->RetainedBytes <= 8 * 1024 * 1024);
    std::cout << "viewer_100k_append_snapshot_model_ms=" << elapsed << " retained_bytes=" << snapshot->RetainedBytes << '\n';
}
}
