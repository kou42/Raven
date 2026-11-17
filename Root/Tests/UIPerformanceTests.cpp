#include "UIPerformanceTests.h"

#include "Raven/Core/Memory/AllocationMeasurement.h"
#include "Raven/Core/Memory/PoolAllocator.h"
#include "Raven/Editor/Panels/InspectorRavenPanel.h"
#include "Raven/Editor/Panels/SceneHierarchyRavenPanel.h"
#include "Raven/Editor/Panels/StatisticsRavenPanel.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIInputNumber.h"
#include "Raven/Editor/Panels/StatisticsActions.h"
#include "Raven/UI/Core/UIContext.h"

#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Rendering/UITessellator.h"
#include "Raven/UI/Rendering/UIGeometryBuilder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
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
    TestUIDrawListCachePerformance();
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

void TestUIDrawListCachePerformance()
{
    constexpr int warmup = 30;
    constexpr int samples = 120;
    std::cout << "cache_baseline,widgets,case,enabled,samples,build_ms,end_frame_ms,layout_ms,drawlist_ms,hits,captures,misses,replayed,cache_bytes,cache_commands,drawlist_bytes,commands,vertices,indices,frame_allocations\n";
    for (const std::size_t count : {100u, 1000u, 10000u})
    {
        for (int scenario = 0; scenario < 3; ++scenario)
        {
            for (bool enabled : {false, true})
            {
                Raven::UIContext context;
                std::vector<Raven::UIPanel*> moving;
                const std::size_t dynamicCount = scenario == 0 ? 0u : scenario == 1 ? count : count / 10u;
                const std::size_t staticCount = count - dynamicCount;
                for (int group = 0; group < 2; ++group)
                {
                    const std::size_t population = group == 0 ? staticCount : dynamicCount;
                    if (population == 0u)
                    {
                        continue;
                    }
                    auto subtree = Raven::CreateScope<Raven::UIElement>();
                    subtree->SetPosition({group == 0 ? 0.0f : 1000.0f, 0.0f});
                    subtree->SetSize({1000.0f, static_cast<float>((population + 9u) / 10u) * 20.0f});
                    subtree->SetDrawListCacheEnabled(enabled);
                    for (std::size_t index = 0; index < population; ++index)
                    {
                        auto panel = Raven::CreateScope<Raven::UIPanel>();
                        panel->SetPosition({static_cast<float>(index % 10u) * 100.0f,
                            static_cast<float>(index / 10u) * 20.0f});
                        panel->SetSize({96.0f, 18.0f});
                        if (group == 1)
                        {
                            moving.push_back(panel.get());
                        }
                        subtree->AddChild(std::move(panel));
                    }
                    context.AddRootChild(std::move(subtree));
                }
                double build = 0.0, endFrame = 0.0, layout = 0.0, draw = 0.0;
                double hits = 0.0, captures = 0.0, misses = 0.0, replayed = 0.0;
                for (int frame = 0; frame < warmup + samples; ++frame)
                {
                    context.BeginFrame({1280.0f, 720.0f}, {1280.0f, 720.0f}, 1.0f, 1.0f);
                    for (auto* panel : moving)
                    {
                        panel->SetSize({95.0f + static_cast<float>(frame % 2), 18.0f});
                        panel->SetBackgroundColor({frame % 2 == 0 ? 0.3f : 0.7f, 0.4f, 0.5f, 1.0f});
                    }
                    const auto started = std::chrono::steady_clock::now();
                    context.EndFrame();
                    const double elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started).count();
                    const auto& value = context.GetLastFrameMetrics();
                    Check(value.DrawCommandCount == count && value.VertexCount == count * 4u &&
                        value.IndexCount == count * 6u, "cache benchmark draw population");
                    if (frame >= warmup)
                    {
                        Check(value.DrawListCache.CaptureCount == 0u && value.DrawListCache.RejectedCount == 0u,
                            "warm cache avoids dynamic snapshot copies");
                        Check(value.DrawListCache.HitCount == (enabled == true && staticCount > 0u ? 1u : 0u),
                            "cache hits only static subtree");
                        build += value.BuildMilliseconds;
                        endFrame += elapsed;
                        layout += value.LayoutMilliseconds;
                        draw += value.DrawListMilliseconds;
                        hits += static_cast<double>(value.DrawListCache.HitCount);
                        captures += static_cast<double>(value.DrawListCache.CaptureCount);
                        misses += static_cast<double>(value.DrawListCache.MissCount);
                        replayed += static_cast<double>(value.DrawListCache.ReplayedCommandCount);
                    }
                }
                const auto& value = context.GetLastFrameMetrics();
                std::cout << "cache_baseline," << count << ',' << (scenario == 0 ? "static" : scenario == 1 ? "dynamic" : "mixed90_10")
                    << ',' << enabled << ',' << samples << ',' << build / samples << ',' << endFrame / samples
                    << ',' << layout / samples << ',' << draw / samples << ',' << hits / samples << ',' << captures / samples
                    << ',' << misses / samples << ',' << replayed / samples << ',' << value.DrawListCacheRetainedBytes
                    << ',' << value.DrawListCacheCommandCount << ',' << value.RetainedAllocationBytes
                    << ',' << value.DrawCommandCount << ',' << value.VertexCount << ',' << value.IndexCount
                    << ',' << value.FrameAllocationCount << '\n';
            }
        }
    }
}

// M4の表現比較専用型です。Runtime Widgetの代替には使いません。
// Legacyは旧UIButtonと同じ保持量、Lazyは個別Overrideだけを別所有する候補を表します。
namespace
{
struct M4StyleOverrides
{
    Raven::UIButtonStyle Style;
    std::uint8_t Mask = 0u;
};
const Raven::UIButtonStyle M4SharedDefaultStyle{};
class M4LegacyButton final : public Raven::UIElement
{
public:
    M4LegacyButton();
    void Configure();
    double Read() const;
private:
    Raven::UIButtonStyle m_Style;
    bool m_Overrides[4]{};
    Raven::UIButton::ClickHandler m_Click;
};
class M4LazyButton final : public Raven::UIElement
{
public:
    M4LazyButton();
    void Configure();
    double Read() const;
private:
    Raven::Scope<M4StyleOverrides> m_Overrides;
    Raven::UIButton::ClickHandler m_Click;
};
M4LegacyButton::M4LegacyButton() { SetFocusable(true); }
M4LazyButton::M4LazyButton() { SetFocusable(true); }
void M4LegacyButton::Configure()
{
    m_Style.NormalColor = {0.11f, 0.12f, 0.13f, 1.0f}; m_Overrides[0] = true; InvalidatePaint();
    m_Style.HoveredColor = {0.21f, 0.22f, 0.23f, 1.0f}; m_Overrides[1] = true; InvalidatePaint();
    m_Style.PressedColor = {0.31f, 0.32f, 0.33f, 1.0f}; m_Overrides[2] = true; InvalidatePaint();
    m_Style.FocusedColor = {0.41f, 0.42f, 0.43f, 1.0f}; m_Overrides[3] = true; InvalidatePaint();
}
void M4LazyButton::Configure()
{
    if (m_Overrides == nullptr)
    {
        m_Overrides = Raven::CreateScope<M4StyleOverrides>();
    }
    m_Overrides->Style.NormalColor = {0.11f, 0.12f, 0.13f, 1.0f}; m_Overrides->Mask |= 1u; InvalidatePaint();
    m_Overrides->Style.HoveredColor = {0.21f, 0.22f, 0.23f, 1.0f}; m_Overrides->Mask |= 2u; InvalidatePaint();
    m_Overrides->Style.PressedColor = {0.31f, 0.32f, 0.33f, 1.0f}; m_Overrides->Mask |= 4u; InvalidatePaint();
    m_Overrides->Style.FocusedColor = {0.41f, 0.42f, 0.43f, 1.0f}; m_Overrides->Mask |= 8u; InvalidatePaint();
}
double M4LegacyButton::Read() const
{
    return m_Style.NormalColor.x + m_Style.HoveredColor.x + m_Style.PressedColor.x + m_Style.FocusedColor.x;
}
double M4LazyButton::Read() const
{
    const auto& style = m_Overrides != nullptr ? m_Overrides->Style : M4SharedDefaultStyle;
    return style.NormalColor.x + style.HoveredColor.x + style.PressedColor.x + style.FocusedColor.x;
}
struct M4WidgetPlan
{
    const char* Name;
    std::size_t ObjectSize;
    std::size_t Alignment;
    std::size_t StyleBytesPerOverride;
    std::size_t SharedBytes;
    bool IsButton;
    Raven::UIElement* (*Create)(void*);
    void (*Destroy)(Raven::UIElement*);
    void (*Configure)(Raven::UIElement*);
    double (*Read)(const Raven::UIElement*);
};
// RAIIは計測用のSlot群だけを所有します。Runtime Treeのunique_ptr所有権は変更しません。
class M4Batch final
{
public:
    M4Batch(const M4WidgetPlan& plan, Raven::PoolAllocator* pool, std::size_t count);
    ~M4Batch();
    void CreateAll(std::size_t count);
    void Clear();
    const std::vector<Raven::UIElement*>& Objects() const;
    std::size_t OwnerBytes() const;
private:
    const M4WidgetPlan& m_Plan;
    Raven::PoolAllocator* m_Pool;
    std::vector<Raven::UIElement*> m_Objects;
};
M4Batch::M4Batch(const M4WidgetPlan& plan, Raven::PoolAllocator* pool, std::size_t count)
    : m_Plan(plan), m_Pool(pool)
{
    m_Objects.reserve(count);
}
M4Batch::~M4Batch() { Clear(); }
void M4Batch::CreateAll(std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index)
    {
        void* slot = m_Pool != nullptr ? m_Pool->Allocate(m_Plan.ObjectSize, m_Plan.Alignment) : nullptr;
        Check(m_Pool == nullptr || slot != nullptr, "M4 pool provides expected slot");
        Raven::UIElement* object = nullptr;
        try { object = m_Plan.Create(slot); }
        catch (...)
        {
            if (m_Pool != nullptr)
            {
                m_Pool->Deallocate(slot);
            }
            throw;
        }
        m_Objects.push_back(object); // reserve済み。pushは確保せず、部分生成もDestructorが回収します。
        Check(object->IsHovered() == false && object->IsPressed() == false && object->IsFocused() == false &&
            object->GetName().empty(), "M4 reused slot starts with fresh instance state");
    }
}
void M4Batch::Clear()
{
    for (auto* object : m_Objects)
    {
        if (m_Pool != nullptr)
        {
            m_Plan.Destroy(object);
            m_Pool->Deallocate(object);
        }
        else
        {
            delete object;
        }
    }
    m_Objects.clear();
}
const std::vector<Raven::UIElement*>& M4Batch::Objects() const { return m_Objects; }
std::size_t M4Batch::OwnerBytes() const { return m_Objects.capacity() * sizeof(Raven::UIElement*); }

void MeasureM4Plan(const M4WidgetPlan& plan, std::size_t count, int overridePercent, bool pooled)
{
    constexpr int warmup = 2;
    constexpr int samples = 5;
    constexpr int readsPerObject = 16;
    Raven::AllocationMeasurement setup;
    Raven::Scope<Raven::PoolAllocator> pool;
    Raven::Scope<M4Batch> batch;
    {
        Raven::AllocationMeasurementScope scope(setup);
        if (pooled == true)
        {
            pool = Raven::CreateScope<Raven::PoolAllocator>(plan.ObjectSize, plan.Alignment, count);
        }
        batch = Raven::CreateScope<M4Batch>(plan, pool.get(), count);
    }
    const std::size_t overrideCount = count * static_cast<std::size_t>(overridePercent) / 100u;
    double createMs = 0.0, configureMs = 0.0, readMs = 0.0, destroyMs = 0.0;
    double createCount = 0.0, createBytes = 0.0, styleCount = 0.0, styleBytes = 0.0, destroyCount = 0.0;
    double checksum = 0.0;
    const auto elapsed = [](auto start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    for (int cycle = 0; cycle < warmup + samples; ++cycle)
    {
        Raven::AllocationMeasurement construction, configuration, destruction;
        double creation, configurationTime, reading, deletion;
        {
            Raven::AllocationMeasurementScope scope(construction);
            const auto start = std::chrono::steady_clock::now();
            batch->CreateAll(count);
            creation = elapsed(start);
        }
        {
            Raven::AllocationMeasurementScope scope(configuration);
            const auto start = std::chrono::steady_clock::now();
            for (std::size_t index = 0; index < overrideCount; ++index)
            {
                plan.Configure(batch->Objects()[index]);
            }
            configurationTime = elapsed(start);
        }
        const auto start = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < readsPerObject; ++repeat)
        {
            for (const auto* object : batch->Objects())
            {
                checksum += plan.Read(object);
            }
        }
        reading = elapsed(start);
        // 再使用時にHover/Pressed等が以前のInstanceから漏れないことも確認します。
        for (auto* object : batch->Objects())
        {
            object->SetHovered(true);
            object->SetPressed(true);
            object->SetName("old-slot");
        }
        {
            Raven::AllocationMeasurementScope scope(destruction);
            const auto begin = std::chrono::steady_clock::now();
            batch->Clear();
            deletion = elapsed(begin);
        }
        Check(pool == nullptr || pool->GetAllocationCount() == 0u, "M4 slot destruction releases every slot");
        if (cycle >= warmup)
        {
            createMs += creation; configureMs += configurationTime; readMs += reading; destroyMs += deletion;
            createCount += static_cast<double>(construction.Count); createBytes += static_cast<double>(construction.Bytes);
            styleCount += static_cast<double>(configuration.Count); styleBytes += static_cast<double>(configuration.Bytes);
            destroyCount += static_cast<double>(destruction.Count);
        }
    }
    const std::size_t poolBytes = pool != nullptr
        ? sizeof(Raven::PoolAllocator) + pool->GetCapacity() + count * (sizeof(std::size_t) + sizeof(bool)) : 0u;
    std::cout << "widget_lifecycle," << plan.Name << ',' << count << ',' << overridePercent << ',' << pooled << ',' << samples
        << ',' << plan.ObjectSize << ',' << count * plan.ObjectSize << ',' << overrideCount * plan.StyleBytesPerOverride
        << ',' << plan.SharedBytes << ',' << poolBytes << ',' << batch->OwnerBytes()
        << ',' << createMs / samples << ',' << configureMs / samples << ',' << readMs / samples << ',' << destroyMs / samples
        << ',' << setup.Available << ',' << setup.Count << ',' << setup.Bytes
        << ',' << createCount / samples << ',' << createBytes / samples << ',' << styleCount / samples << ',' << styleBytes / samples
        << ',' << destroyCount / samples << ',' << checksum << '\n';
}
struct M4PanelButtons
{
    std::size_t Count = 0u;
    std::size_t Overridden = 0u;
};
void CountM4Buttons(const Raven::UIElement& element, M4PanelButtons& counts)
{
    if (const auto* button = dynamic_cast<const Raven::UIButton*>(&element); button != nullptr)
    {
        ++counts.Count;
        const auto* context = button->GetContext();
        Check(context != nullptr, "M4 panel fixture buttons belong to a context");
        const auto& style = context->GetTheme().Button;
        // 値が同じでも明示Overrideは将来のTheme追従が異なるため、参照先の共有を調べます。
        if (&button->GetNormalColor() != &style.NormalColor || &button->GetHoveredColor() != &style.HoveredColor ||
            &button->GetPressedColor() != &style.PressedColor || &button->GetFocusedColor() != &style.FocusedColor)
        {
            ++counts.Overridden;
        }
    }
    for (const auto& child : element.GetChildren())
    {
        CountM4Buttons(*child, counts);
    }
}
} // namespace

void TestUIWidgetLifecycleMeasurements()
{
    Check(sizeof(Raven::UIButton) == sizeof(M4LegacyButton) || sizeof(Raven::UIButton) == sizeof(M4LazyButton),
        "M4 actual Button size agrees with one of the measured representations");
    std::cout << "widget_lifecycle,kind,widgets,override_percent,pooled,samples,object_size,object_bytes,style_bytes,shared_bytes,pool_reserved_bytes,owner_bytes,create_ms,configure_ms,read16_ms,destroy_ms,allocation_available,setup_allocations,setup_allocated_bytes,create_allocations,create_allocated_bytes,override_allocations,override_allocated_bytes,destroy_allocations,checksum\n";
    const bool actualLazy = sizeof(Raven::UIButton) == sizeof(M4LazyButton);
    const M4WidgetPlan plans[] =
    {
        {"button", sizeof(Raven::UIButton), alignof(Raven::UIButton), actualLazy ? sizeof(M4StyleOverrides) : 0u, actualLazy ? sizeof(Raven::UIButtonStyle) : 0u, true,
            [](void* slot) -> Raven::UIElement*
            {
                if (slot == nullptr)
                {
                    return new Raven::UIButton();
                }
                return std::construct_at(static_cast<Raven::UIButton*>(slot));
            },
            [](Raven::UIElement* object) { std::destroy_at(static_cast<Raven::UIButton*>(object)); },
            [](Raven::UIElement* object)
            {
                auto* button = static_cast<Raven::UIButton*>(object);
                button->SetNormalColor({0.11f, 0.12f, 0.13f, 1.0f});
                button->SetHoveredColor({0.21f, 0.22f, 0.23f, 1.0f});
                button->SetPressedColor({0.31f, 0.32f, 0.33f, 1.0f});
                button->SetFocusedColor({0.41f, 0.42f, 0.43f, 1.0f});
            },
            [](const Raven::UIElement* object)
            {
                const auto* button = static_cast<const Raven::UIButton*>(object);
                return static_cast<double>(button->GetNormalColor().x + button->GetHoveredColor().x +
                    button->GetPressedColor().x + button->GetFocusedColor().x);
            }},
        {"legacy_model", sizeof(M4LegacyButton), alignof(M4LegacyButton), 0u, 0u, true,
            [](void* slot) -> Raven::UIElement*
            {
                if (slot == nullptr)
                {
                    return new M4LegacyButton();
                }
                return std::construct_at(static_cast<M4LegacyButton*>(slot));
            },
            [](Raven::UIElement* object) { std::destroy_at(static_cast<M4LegacyButton*>(object)); },
            [](Raven::UIElement* object) { static_cast<M4LegacyButton*>(object)->Configure(); },
            [](const Raven::UIElement* object) { return static_cast<const M4LegacyButton*>(object)->Read(); }},
        {"lazy_model", sizeof(M4LazyButton), alignof(M4LazyButton), sizeof(M4StyleOverrides), sizeof(Raven::UIButtonStyle), true,
            [](void* slot) -> Raven::UIElement*
            {
                if (slot == nullptr)
                {
                    return new M4LazyButton();
                }
                return std::construct_at(static_cast<M4LazyButton*>(slot));
            },
            [](Raven::UIElement* object) { std::destroy_at(static_cast<M4LazyButton*>(object)); },
            [](Raven::UIElement* object) { static_cast<M4LazyButton*>(object)->Configure(); },
            [](const Raven::UIElement* object) { return static_cast<const M4LazyButton*>(object)->Read(); }},
        {"panel", sizeof(Raven::UIPanel), alignof(Raven::UIPanel), 0u, 0u, false,
            [](void* slot) -> Raven::UIElement*
            {
                if (slot == nullptr)
                {
                    return new Raven::UIPanel();
                }
                return std::construct_at(static_cast<Raven::UIPanel*>(slot));
            },
            [](Raven::UIElement* object) { std::destroy_at(static_cast<Raven::UIPanel*>(object)); },
            [](Raven::UIElement*) {},
            [](const Raven::UIElement* object)
            {
                return static_cast<double>(object->GetChildren().size()) + (object->IsFocusable() == true ? 1.0 : 0.0);
            }},
        {"label", sizeof(Raven::UILabel), alignof(Raven::UILabel), 0u, 0u, false,
            [](void* slot) -> Raven::UIElement*
            {
                if (slot == nullptr)
                {
                    return new Raven::UILabel();
                }
                return std::construct_at(static_cast<Raven::UILabel*>(slot));
            },
            [](Raven::UIElement* object) { std::destroy_at(static_cast<Raven::UILabel*>(object)); },
            [](Raven::UIElement*) {},
            [](const Raven::UIElement* object)
            {
                return static_cast<double>(object->GetChildren().size()) + (object->IsFocusable() == true ? 1.0 : 0.0);
            }},
        {"number", sizeof(Raven::UIInputNumber), alignof(Raven::UIInputNumber), 0u, 0u, false,
            [](void* slot) -> Raven::UIElement*
            {
                if (slot == nullptr)
                {
                    return new Raven::UIInputNumber();
                }
                return std::construct_at(static_cast<Raven::UIInputNumber*>(slot));
            },
            [](Raven::UIElement* object) { std::destroy_at(static_cast<Raven::UIInputNumber*>(object)); },
            [](Raven::UIElement*) {},
            [](const Raven::UIElement* object)
            {
                return static_cast<double>(object->GetChildren().size()) + (object->IsFocusable() == true ? 1.0 : 0.0);
            }},
        {"scroll", sizeof(Raven::UIScrollView), alignof(Raven::UIScrollView), 0u, 0u, false,
            [](void* slot) -> Raven::UIElement*
            {
                if (slot == nullptr)
                {
                    return new Raven::UIScrollView();
                }
                return std::construct_at(static_cast<Raven::UIScrollView*>(slot));
            },
            [](Raven::UIElement* object) { std::destroy_at(static_cast<Raven::UIScrollView*>(object)); },
            [](Raven::UIElement*) {},
            [](const Raven::UIElement* object)
            {
                return static_cast<double>(object->GetChildren().size()) + (object->IsFocusable() == true ? 1.0 : 0.0);
            }},
    };
    for (const auto& plan : plans)
    {
        for (std::size_t count : {100u, 1000u, 10000u})
        {
            for (int percent : {0, 10, 100})
            {
                if (plan.IsButton == false && percent != 0)
                {
                    continue;
                }
                for (bool pooled : {false, true})
                {
                    MeasureM4Plan(plan, count, percent, pooled);
                }
            }
        }
    }
    // 代表Editor Panelの実構築で、ButtonがTheme共有か個別Overrideかを観測します。
    Raven::UIContext context;
    Raven::InspectorRavenPanel inspector;
    Raven::SceneHierarchyRavenPanel hierarchy;
    Raven::StatisticsRavenPanel statistics;
    Check(inspector.Attach(context, nullptr) && hierarchy.Attach(context, nullptr) && statistics.Attach(context, nullptr),
        "M4 representative panel fixture attaches without GPU/font");
    M4PanelButtons counts;
    CountM4Buttons(context.GetRootElement(), counts);
    Check(counts.Count > 0u, "M4 representative fixture contains buttons");
    std::cout << "widget_panel,inspector_hierarchy_statistics," << counts.Count << ',' << counts.Overridden << '\n';
    statistics.Detach(); hierarchy.Detach(); inspector.Detach();
}

void MeasureUIGeometryBatches()
{
    std::cout << "geometry,commands,clip_pattern,merge,samples,build_ms,batches,vertices,indices\n";
    for (std::size_t count : {100u, 1000u, 10000u})
    {
        for (bool alternating : {false, true})
        {
            Raven::UIDrawList list;
            for (std::size_t i = 0; i < count; ++i)
            {
                list.AddRect({0.0f, 0.0f}, {20.0f, 20.0f}, {0.1f, 0.2f, 0.3f, 0.5f});
                if (alternating == true && i % 2u == 0u)
                {
                    list.ApplyClip(i, Raven::UIClipRect::FromRect({{1.1f, 1.1f}, {19.1f, 19.1f}}));
                }
            }
            for (bool merge : {false, true})
            {
                double total = 0.0;
                Raven::UIRenderGeometry geometry;
                constexpr int warmup = 5, samples = 30;
                for (int frame = 0; frame < warmup + samples; ++frame)
                {
                    const auto start = std::chrono::steady_clock::now();
                    Check(Raven::UIGeometryBuilder::Build(list, {1280.0f, 720.0f}, 1600u, 900u, geometry, merge), "geometry measurement builds");
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    Check(geometry.Batches.size() == (merge == true && alternating == false ? 1u : count), "geometry measurement batch population");
                    if (frame >= warmup)
                    {
                        total += ms;
                    }
                }
                std::cout << "geometry," << count << ',' << (alternating == true ? "alternating" : "same")
                    << ',' << merge << ',' << samples << ',' << total / samples << ',' << geometry.Batches.size()
                    << ',' << geometry.Vertices.size() << ',' << geometry.Indices.size() << '\n';
            }
        }
    }
}
