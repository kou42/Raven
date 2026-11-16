#include "UIPerformanceTests.h"

#include "Raven/Core/Memory/AllocationMeasurement.h"
#include "Raven/Editor/Panels/StatisticsActions.h"
#include "Raven/UI/Core/UIContext.h"

#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Rendering/UITessellator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <thread>
#include <vector>

namespace
{
void Check(bool condition, const char* message)
{
    if (condition == false)
    {
        std::cerr << "[FAIL] UI performance: " << message << '\n';
        std::exit(1);
    }
}

void CheckNear(double actual, double expected, const char* message)
{
    Check(std::abs(actual - expected) < 0.00001, message);
}
} // namespace

void TestUIAllocationMeasurement()
{
    const bool enabled = Raven::IsAllocationMeasurementEnabled();
    Raven::AllocationMeasurement outer;
    Raven::AllocationMeasurement inner;
    void* plain = nullptr;
    {
        Raven::AllocationMeasurementScope scope(outer);
        // new式のAllocation Elisionを避け、置換可能APIそのものを検証します。
        plain = ::operator new(37u);
        {
            Raven::AllocationMeasurementScope same(outer);
            void* array = ::operator new[](29u, std::nothrow);
            Check(array != nullptr, "nothrow array allocation");
            ::operator delete[](array, std::nothrow);
        }
        {
            Raven::AllocationMeasurementScope nested(inner);
            void* aligned = ::operator new(17u, std::align_val_t(64u), std::nothrow);
            Check(aligned != nullptr && reinterpret_cast<std::uintptr_t>(aligned) % 64u == 0u,
                "aligned nothrow allocation contract");
            ::operator delete(aligned, std::align_val_t(64u), std::nothrow);
        }
        void* zero = ::operator new(0u);
        Check(zero != nullptr, "zero size new is non-null");
        ::operator delete(zero);
    }
    ::operator delete(plain, 37u);
    Check(outer.Available == enabled && inner.Available == enabled, "availability follows build mode");
    Check(outer.Count == (enabled == true ? 3u : 0u), "nested same target counts once and resumes outer");
    Check(outer.Bytes == (enabled == true ? 66u : 0u), "explicit allocation bytes");
    Check(inner.Count == (enabled == true ? 1u : 0u) && inner.Bytes == (enabled == true ? 17u : 0u),
        "nested different target is exclusive");

    Raven::AllocationMeasurement fundamental;
    {
        Raven::AllocationMeasurementScope scope(fundamental);
        void* smallAligned = ::operator new(3u, std::align_val_t(1u));
        Check(smallAligned != nullptr, "fundamental aligned new contract");
        ::operator delete(smallAligned, std::align_val_t(1u));
    }
    Check(fundamental.Count == (enabled == true ? 1u : 0u), "small alignment allocation measured once");

    // STLのDebug用Proxy等の実装差があるため、内部Allocation回数の固定値は要求しません。
    Raven::AllocationMeasurement stl;
    std::vector<int> temporary;
    {
        Raven::AllocationMeasurementScope scope(stl);
        temporary.reserve(100u);
    }
    Check(enabled == true ? (stl.Count >= 1u && stl.Bytes >= 100u * sizeof(int)) : stl.Count == 0u,
        "STL temporary allocations outside DrawList are measured");

    Raven::AllocationMeasurement afterException;
    {
        Raven::AllocationMeasurementScope scope(afterException);
        try
        {
            Raven::AllocationMeasurementScope nested(inner);
            throw 1;
        }
        catch (int)
        {
        }
        void* value = ::operator new(7u);
        ::operator delete(value);
    }
    Check(afterException.Count == (enabled == true ? 1u : 0u), "exception restores active collector");
    const auto oldCount = outer.Count;
    void* unscoped = ::operator new(13u);
    ::operator delete(unscoped);
    Check(outer.Count == oldCount, "unscoped allocations excluded");

    Raven::AllocationMeasurement worker;
    std::thread thread([&worker]()
    {
        Raven::AllocationMeasurementScope scope(worker);
        void* value = ::operator new[](53u, std::align_val_t(128u));
        Check(reinterpret_cast<std::uintptr_t>(value) % 128u == 0u, "aligned array allocation contract");
        ::operator delete[](value, 53u, std::align_val_t(128u));
    });
    thread.join();
    Check(worker.Count == (enabled == true ? 1u : 0u) && outer.Count == oldCount,
        "thread-local scope isolation");

    // 成功した確保だけを加算し、通常new_handlerとnothrowの失敗契約を維持します。
    static int handlerCalls = 0;
    handlerCalls = 0;
    const auto oldHandler = std::set_new_handler([]()
    {
        ++handlerCalls;
        std::set_new_handler(nullptr);
    });
    Raven::AllocationMeasurement failure;
    void* failed = nullptr;
    {
        Raven::AllocationMeasurementScope scope(failure);
        failed = ::operator new(std::numeric_limits<std::size_t>::max(), std::nothrow);
    }
    std::set_new_handler(oldHandler);
    Check(failed == nullptr && handlerCalls == 1 && failure.Count == 0u,
        "allocation failure retries handler then returns null without counting");
}

void TestUIWindowRecording()
{
    Raven::UICompletedFrameMetrics completed;
    completed.FrameIndex = 101u;
    Raven::UIFrameMetrics main;
    main.Available = true;
    main.FrameIndex = 3u;
    main.BuildMilliseconds = 1.0;
    main.DrawCommandCount = 10u;
    main.VertexCount = 40u;
    main.IndexCount = 60u;
    main.TextureSwitchCount = 2u;
    main.FrameAllocationCount = 5u;
    main.FrameAllocatedBytes = 80u;
    main.RetainedAllocationBytes = 160u;
    main.ElementCount = 7u;
    main.PeakElementCount = 9u;
    main.CppAllocations = { true, 1u, 16u };
    auto auxiliary = main;
    auxiliary.FrameIndex = 12u;
    auxiliary.BuildMilliseconds = 3.0;
    auxiliary.DrawCommandCount = 20u;
    auxiliary.CppAllocations = { true, 3u, 48u };
    completed.Windows = {{10u, true, main}, {20u, false, auxiliary}, {10u, true, main}};
    auto unavailable = main;
    unavailable.Available = false;
    completed.Windows.push_back({30u, false, unavailable});
    Raven::StatisticsSnapshot snapshot;
    Raven::BuildStatisticsUIWindowSnapshots(completed, snapshot);
    Check(snapshot.UIWindows.size() == 2u, "skip duplicate and unavailable Windows");
    Check(snapshot.UI.FrameIndex == 3u && snapshot.UIAllWindows.FrameIndex == 101u,
        "independent Context and Application frame indices");
    const auto& total = snapshot.UIAllWindows;
    CheckNear(total.BuildMilliseconds, 4.0, "sum window CPU build duration");
    Check(total.DrawCommandCount == 30u && total.VertexCount == 80u && total.IndexCount == 120u &&
        total.TextureSwitchCount == 4u && total.FrameAllocationCount == 10u &&
        total.FrameAllocatedBytes == 160u && total.RetainedAllocationBytes == 320u &&
        total.ElementCount == 14u && total.PeakElementCount == 14u, "sum all window counters");
    Check(total.CppAllocations.Available && total.CppAllocations.Count == 4u && total.CppAllocations.Bytes == 64u,
        "sum measured C++ allocations");

    Raven::StatisticsUIRecordingSession recording;
    recording.Start();
    recording.AddSample(snapshot);
    recording.AddSample(snapshot);
    Check(recording.GetSampleCount() == 1u && recording.GetWindows().at(20u).GetSampleCount() == 1u,
        "duplicate Application frame is ignored for every stream");
    completed.FrameIndex = 102u;
    completed.Windows.resize(1u); // 補助Window最小化/Close: 古い完了Frameを再供給しません。
    completed.Windows[0].Metrics.FrameIndex = 4u;
    completed.Windows[0].Metrics.BuildMilliseconds = 2.0;
    completed.Windows[0].Metrics.CppAllocations.Available = false;
    completed.PeakElementCount = 14u;
    Raven::BuildStatisticsUIWindowSnapshots(completed, snapshot);
    Check(snapshot.UIAllWindows.CppAllocations.Available == false, "unmeasured is not zero");
    recording.AddSample(snapshot);
    Check(recording.GetWindows().at(20u).GetSampleCount() == 1u, "skipped window has no duplicate or zero sample");
    CheckNear(recording.GetTotal().GetAverages()[0], 3.0, "aggregate average counts Application frames");
    Check(recording.GetTotal().GetMetricSampleCount(10u) == 1u,
        "unmeasured allocation frames excluded from denominator");
    Check(snapshot.UIAllWindows.PeakElementCount == 14u, "aggregate element peak survives Window close");

    completed.FrameIndex = 103u;
    completed.Windows[0].Metrics.FrameIndex = 5u;
    auxiliary.FrameIndex = 1u;
    auxiliary.BuildMilliseconds = 4.0;
    auxiliary.CppAllocations.Available = false;
    completed.Windows.push_back({99u, false, auxiliary});
    Raven::BuildStatisticsUIWindowSnapshots(completed, snapshot);
    recording.AddSample(snapshot);
    Check(recording.GetWindows().size() == 3u && recording.GetWindows().at(99u).GetSampleCount() == 1u,
        "new Window ID starts independent stream while closed Window retained");
    CheckNear(recording.GetTotal().GetAverages()[0], 4.0, "aggregate average after Window replacement");

    for (std::size_t index = 0u; index < Raven::StatisticsUIRecordingSession::MaxWindowStreams; ++index)
    {
        ++completed.FrameIndex;
        completed.Windows = {{1000u + index, false, auxiliary}};
        Raven::BuildStatisticsUIWindowSnapshots(completed, snapshot);
        recording.AddSample(snapshot);
    }
    Check(recording.GetWindows().size() == Raven::StatisticsUIRecordingSession::MaxWindowStreams &&
        recording.GetOmittedWindowSamples() == 3u, "bound per-Window history but continue aggregate");
    std::string message;
    Check(recording.SaveCSV("", message) == false, "cannot save active multiwindow session");
    recording.Stop();
    const auto sampleCount = recording.GetSampleCount();
    recording.AddSample(snapshot);
    Check(recording.GetSampleCount() == sampleCount, "stopped session rejects samples");
    namespace fs = std::filesystem;
    const fs::path directory = fs::temp_directory_path() / ("raven-multiwindow-recording-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(directory);
    const fs::path output = directory / "session.csv";
    Check(recording.SaveCSV(output.string(), message), "save multiwindow CSV");
    std::ifstream stream(output);
    const std::string csv((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();
    Check(csv.find("all_windows_ui,build_ms,") != std::string::npos &&
        csv.find("window_20,build_ms,1,3,3") != std::string::npos &&
        csv.find("window_10,cpp_allocations,1,1,1") != std::string::npos &&
        csv.find("window_99,cpp_allocations,0,N/A,N/A") != std::string::npos &&
        csv.find("recording,omitted_window_samples,1,3,3") != std::string::npos,
        "CSV distinguishes aggregate, closed Windows, availability and omitted history");
    Check(recording.SaveCSV(output.string(), message) == false, "refuse existing CSV path");
    fs::remove_all(directory);
    recording.Start();
    Check(recording.GetSampleCount() == 0u && recording.GetWindows().empty() &&
        recording.GetOmittedWindowSamples() == 0u, "restart clears all streams");
    completed.Windows.clear();
    Raven::BuildStatisticsUIWindowSnapshots(completed, snapshot);
    recording.AddSample(snapshot);
    Check(snapshot.UIAllWindows.Available == false && recording.GetSampleCount() == 0u,
        "empty batch does not publish stale frame");
}

#if defined(RAVEN_UI_PERFORMANCE_TEST_MAIN)
int main()
{
    TestUIAllocationMeasurement();
    TestUIWindowRecording();
    TestUIModernizationBaseline();
    std::cout << "[PASS] UI multiwindow and allocation tests (profiling "
        << (Raven::IsAllocationMeasurementEnabled() == true ? "enabled" : "disabled") << ")\n";
}
#endif

// 所有権はContext→ScrollView→Content→Panel。測定用pointerはContextより先に破棄します。
void TestUIModernizationBaseline()
{
    constexpr int warmupFrames = 30;
    constexpr int sampleFrames = 120;
    std::cout << "baseline,widgets,case,samples,build_ms,layout_ms,drawlist_ms,metrics_ms,geometry_ms,commands,vertices,indices,elements,frame_allocations,retained_bytes,cpp_available,cpp_count,cpp_bytes\n";
    for (const std::size_t count : {100u, 1000u, 10000u})
    {
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            Raven::UIContext context;
            auto content = Raven::CreateScope<Raven::UIElement>();
            content->SetSize(Raven::math::Vec2(1000.0f, static_cast<float>(count / 10u) * 20.0f));
            std::vector<Raven::UIPanel*> panels;
            for (std::size_t index = 0; index < count; ++index)
            {
                auto panel = Raven::CreateScope<Raven::UIPanel>();
                panel->SetPosition(Raven::math::Vec2(static_cast<float>(index % 10u) * 100.0f,
                    static_cast<float>(index / 10u) * 20.0f));
                panel->SetSize(Raven::math::Vec2(96.0f, 18.0f));
                panels.push_back(panel.get());
                content->AddChild(std::move(panel));
            }
            auto scroll = Raven::CreateScope<Raven::UIScrollView>();
            scroll->SetSize(Raven::math::Vec2(1000.0f, 160.0f));
            scroll->SetContent(std::move(content));
            auto* scrollView = scroll.get();
            context.AddRootChild(std::move(scroll));
            double build = 0.0, layout = 0.0, draw = 0.0, metrics = 0.0, geometry = 0.0;
            double allocations = 0.0, bytes = 0.0;
            for (int frame = 0; frame < warmupFrames + sampleFrames; ++frame)
            {
                context.BeginFrame(Raven::math::Vec2(1280.0f, 720.0f),
                    Raven::math::Vec2(1280.0f, 720.0f), 1.0f, 1.0f);
                if (scenario == 1)
                {
                    // 全Widgetのサイズと色を更新し、LayoutとPaintの両方を毎Frame変更します。
                    for (auto* panel : panels)
                    {
                        panel->SetSize(Raven::math::Vec2(95.0f + static_cast<float>(frame % 2), 18.0f));
                        panel->SetBackgroundColor(Raven::math::Vec4(frame % 2 == 0 ? 0.3f : 0.7f, 0.4f, 0.5f, 1.0f));
                    }
                }
                if (scenario == 2)
                {
                    scrollView->SetScrollOffset(Raven::math::Vec2(0.0f, static_cast<float>(frame % 20) * 2.0f));
                }
                context.EndFrame();
                const auto& value = context.GetLastFrameMetrics();
                Check(value.Available && value.ElementCount >= count + 3u, "baseline retained population");
                Check(value.RendererAvailable == false && value.RendererMilliseconds == 0.0,
                    "headless renderer is unavailable");
                Check(std::isfinite(value.BuildMilliseconds) && value.LayoutMilliseconds >= 0.0 &&
                    value.DrawListMilliseconds >= 0.0 && value.CommandMetricsMilliseconds >= 0.0,
                    "finite nonnegative timing boundaries");
                CheckNear(value.BuildMilliseconds, value.LayoutMilliseconds + value.DrawListMilliseconds +
                    value.CommandMetricsMilliseconds, "build timing partition");
                // Rendererと同じく出力をFrameごとに生成。Geometry確保時間も含みます。
                const auto started = std::chrono::steady_clock::now();
                Raven::UITessellatedDrawList triangles;
                Check(Raven::UITessellator::Tessellate(context.GetDrawList(), triangles), "baseline tessellation");
                const double elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                Check(triangles.Vertices.size() == value.VertexCount && triangles.Indices.size() == value.IndexCount,
                    "estimated geometry matches actual conversion");
                if (frame >= warmupFrames)
                {
                    build += value.BuildMilliseconds;
                    layout += value.LayoutMilliseconds;
                    draw += value.DrawListMilliseconds;
                    metrics += value.CommandMetricsMilliseconds;
                    geometry += elapsed;
                    allocations += static_cast<double>(value.CppAllocations.Count);
                    bytes += static_cast<double>(value.CppAllocations.Bytes);
                }
            }
            const auto& value = context.GetLastFrameMetrics();
            std::cout << "baseline," << count << ',' << (scenario == 0 ? "static" : scenario == 1 ? "update" : "scroll")
                << ',' << sampleFrames << ',' << build / sampleFrames << ',' << layout / sampleFrames
                << ',' << draw / sampleFrames << ',' << metrics / sampleFrames << ',' << geometry / sampleFrames
                << ',' << value.DrawCommandCount << ',' << value.VertexCount << ',' << value.IndexCount
                << ',' << value.ElementCount << ',' << value.FrameAllocationCount << ',' << value.RetainedAllocationBytes
                << ',' << value.CppAllocations.Available << ',' << allocations / sampleFrames << ',' << bytes / sampleFrames << '\n';
        }
    }
}
