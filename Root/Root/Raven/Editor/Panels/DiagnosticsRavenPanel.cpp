#include "Raven/Editor/Panels/DiagnosticsRavenPanel.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Widgets/UIWindow.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIInputText.h"
#include "Raven/UI/Widgets/UIComboBox.h"
#include "Raven/UI/Widgets/UICheckbox.h"
#include "Raven/UI/Widgets/UITable.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <sstream>

namespace Raven
{
namespace
{
constexpr std::size_t MaxTimelineThreads = 32;
constexpr std::size_t MaxTimelineIntervals = 2048;
constexpr float SnapshotIntervalSeconds = 0.2f;
// 純粋なSnapshot描画です。計測APIを呼ぶと自身の描画を再び収集するため呼びません。
class DiagnosticChart final : public UIElement
{
public:
    DiagnosticChart(const DiagnosticsViewModel& model, bool timeline) : m_Model(model), m_Timeline(timeline) {}
protected:
    void OnBuildDrawList(UIDrawList& list, const math::Vec2& position) const override
    {
        const auto size = GetSize();
        list.AddRect(position, position + size, {0.08f, 0.09f, 0.12f, 1.0f});
        if (m_Timeline == false)
        {
            const auto& points = m_Model.GetCounterGraph();
            if (points.empty() == true) { return; }
            double low = points.front().Value, high = low;
            for (const auto& point : points) { low = std::min(low, point.Value); high = std::max(high, point.Value); }
            // 先に差を取ることで、長時間稼働時の絶対Timestampをfloatへ丸めません。
            const double scale = std::max({1.0, std::abs(low), std::abs(high)});
            const auto origin = points.front().TimestampNanoseconds;
            const double span = static_cast<double>(std::max<std::uint64_t>(1, points.back().TimestampNanoseconds - origin));
            for (const auto& point : points)
            {
                const float x = 5.0f + static_cast<float>(static_cast<double>(point.TimestampNanoseconds - origin) / span) * (size.x - 10.0f);
                const float y = high == low ? size.y * 0.5f : 5.0f + static_cast<float>((high / scale - point.Value / scale) / (high / scale - low / scale)) * (size.y - 10.0f);
                const bool selected = m_Model.GetFilter().Frame == point.FrameId;
                list.AddRect(position + math::Vec2(x - 2, y - 2), position + math::Vec2(x + 2, y + 2),
                    selected ? math::Vec4(1, 0.8f, 0.2f, 1) : math::Vec4(0.2f, 0.8f, 0.9f, 1));
            }
            return;
        }
        const auto& intervals = m_Model.GetTimeline().Intervals;
        if (intervals.empty() == true) { return; }
        std::uint64_t origin = UINT64_MAX, end = 0;
        std::map<std::uint64_t, std::size_t> lanes;
        for (const auto& interval : intervals)
        {
            origin = std::min(origin, interval.BeginNanoseconds == 0 ? interval.EndNanoseconds : interval.BeginNanoseconds);
            end = std::max(end, std::max(interval.BeginNanoseconds, interval.EndNanoseconds));
            if (lanes.size() < MaxTimelineThreads || lanes.contains(interval.ThreadId)) { lanes.try_emplace(interval.ThreadId, lanes.size()); }
        }
        const double span = static_cast<double>(std::max<std::uint64_t>(1, end - origin));
        const float laneHeight = size.y / static_cast<float>(lanes.size());
        std::size_t drawn = 0;
        // Thread順に整列済みの末尾が非表示Threadだけでも、表示対象Laneの区間を失わないよう、
        // Laneを選別してから描画件数を制限します。
        for (std::size_t index = intervals.size(); index > 0 && drawn < MaxTimelineIntervals; --index)
        {
            const auto& interval = intervals[index - 1];
            const auto lane = lanes.find(interval.ThreadId);
            if (lane == lanes.end()) { continue; }
            ++drawn;
            const float x = static_cast<float>(static_cast<double>((interval.BeginNanoseconds == 0 ? interval.EndNanoseconds : interval.BeginNanoseconds) - origin) / span) * (size.x - 4);
            // 欠落したEndに推定時間を補いません。橙の点は不完全な記録です。
            const float width = interval.Complete == true
                ? std::max(2.0f, static_cast<float>(static_cast<double>(interval.EndNanoseconds - interval.BeginNanoseconds) / span) * (size.x - 4)) : 3.0f;
            const float y = static_cast<float>(lane->second) * laneHeight + (interval.ParentScopeId == 0 ? 1.0f : laneHeight * 0.5f);
            list.AddRect(position + math::Vec2(x, y), position + math::Vec2(x + width, y + std::max(2.0f, laneHeight * 0.4f)),
                interval.Complete == true ? math::Vec4(0.3f, 0.7f, 0.4f, 1) : math::Vec4(1, 0.5f, 0.1f, 1));
        }
    }
private:
    const DiagnosticsViewModel& m_Model;
    bool m_Timeline;
};
}

bool DiagnosticsRavenPanel::Attach(UIContext& context, const Ref<UIFontAtlas>& font,
    std::shared_ptr<Diagnostics::DiagnosticEventStore> store)
{
    if (m_Root != nullptr) { return false; }
    m_Store = store != nullptr ? std::move(store) : Diagnostics::GetEditorDiagnosticEventStore();
    auto window = CreateScope<UIWindow>();
    window->SetTitle("Diagnostics");
    window->SetSize({1040, 720});
    window->SetPreferredSize({1040, 720});
    auto scroll = CreateScope<UIScrollView>();
    scroll->SetPosition({8, 30});
    scroll->SetSize({1020, 680});
    scroll->SetHorizontalAlignment(UIAlignment::Stretch);
    scroll->SetVerticalAlignment(UIAlignment::Stretch);
    UIThickness margin; margin.Left = 8; margin.Right = 8; margin.Top = 30; margin.Bottom = 8;
    scroll->SetMargin(margin);
    auto content = CreateScope<UIElement>();
    content->SetSize({1000, 1800});
    content->SetPreferredSize({1000, 1800});
    auto label = [&](const char* text, float y, float height = 24.0f)
    {
        auto item = CreateScope<UILabel>(); item->SetFont(font); item->SetText(text);
        item->SetPosition({8, y}); item->SetSize({980, height});
        return static_cast<UILabel*>(content->AddChild(std::move(item)));
    };
    label("Search (log only)                 Severity                Category                 Frame (blank=all, ?=unknown)     Thread (blank=all)", 4);
    auto input = [&](float x, float width, auto changed)
    {
        auto item = CreateScope<UIInputText>(); item->SetFont(font); item->SetPosition({x, 30}); item->SetSize({width, 28});
        item->SetOnChange(changed); content->AddChild(std::move(item));
    };
    input(8, 240, [this](const std::string& value) { auto f = m_Model.GetFilter(); f.Search = value; m_Model.SetFilter(std::move(f)); Refresh(); });
    auto combo = [&](float x, float y, std::vector<std::string> options, auto changed)
    {
        auto item = CreateScope<UIComboBox>(); item->SetFont(font); item->SetPosition({x, y}); item->SetSize({160, 28});
        item->SetOptions(std::move(options)); item->SetSelectedIndex(0); item->SetOnSelectionChanged(changed);
        return static_cast<UIComboBox*>(content->AddChild(std::move(item)));
    };
    combo(256, 30, {"Info+", "Warning+", "Error"}, [this](std::size_t index, const std::string&)
    { auto f = m_Model.GetFilter(); f.MinimumSeverity = static_cast<Diagnostics::DiagnosticSeverity>(index); m_Model.SetFilter(std::move(f)); Refresh(); });
    combo(424, 30, {"All", "Core", "RHI", "Renderer", "Physics", "Animation", "Asset", "Runtime", "SoftBody"}, [this](std::size_t index, const std::string&)
    { auto f = m_Model.GetFilter(); f.Category.reset(); if (index > 0) { f.Category = static_cast<Diagnostics::DiagnosticCategory>(index - 1); } m_Model.SetFilter(std::move(f)); Refresh(); });
    auto identifier = [this](const std::string& value, bool frame)
    {
        std::optional<std::uint64_t> parsed;
        const bool unknown = frame == true && value == "?";
        if (value.empty() == false && unknown == false)
        {
            std::uint64_t number = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            {
                m_FilterError = "Invalid Frame/Thread input; previous valid filter remains active.";
                Refresh();
                return;
            }
            parsed = number;
        }
        m_FilterError.clear();
        auto f = m_Model.GetFilter();
        if (frame == true) { f.Frame = parsed; f.UnassociatedOnly = unknown; } else { f.Thread = parsed; }
        m_Model.SetFilter(std::move(f)); Refresh();
    };
    input(592, 180, [identifier](const std::string& value) { identifier(value, true); });
    input(780, 200, [identifier](const std::string& value) { identifier(value, false); });
    auto check = [&](float x, const char* caption, bool checked, auto changed)
    {
        auto item = CreateScope<UICheckbox>(); item->SetCaption(caption, font); item->SetChecked(checked);
        item->SetPosition({x, 66}); item->SetSize({240, 26}); item->SetOnToggle(changed); content->AddChild(std::move(item));
    };
    check(8, "Capture measurements", Diagnostics::IsMeasurementsEnabled(), [](bool enabled) { Diagnostics::SetMeasurementsEnabled(enabled); });
    check(256, "Freeze view (capture continues)", false, [this](bool frozen) { m_Frozen = frozen; });
    check(592, "Show measurement rows", false, [this](bool enabled) { auto f = m_Model.GetFilter(); f.IncludeMeasurements = enabled; m_Model.SetFilter(std::move(f)); Refresh(); });
    m_Status = label("Waiting for diagnostics", 100, 90); m_Status->SetWrapMode(UITextWrapMode::Word);
    auto table = CreateScope<UITable>(); table->SetFont(font); table->SetPosition({8, 194}); table->SetSize({980, 300});
    table->AddColumn("Severity", 80); table->AddColumn("Category", 90); table->AddColumn("Frame", 90); table->AddColumn("Thread", 90); table->AddColumn("Message", 610);
    table->SetDataSource([this]() { return m_Model.GetRows().size(); }, [this](std::size_t row, std::size_t column) { return Cell(row, column); });
    table->SetOnSelectionChanged([this](std::size_t row) { if (m_Updating == false) { m_Model.SelectRow(row); Refresh(); } });
    m_Table = static_cast<UITable*>(content->AddChild(std::move(table)));
    auto detailLabel = CreateScope<UILabel>(); detailLabel->SetFont(font);
    detailLabel->SetText("Select an event"); detailLabel->SetSize({950, 360});
    detailLabel->SetWrapMode(UITextWrapMode::Word);
    m_Detail = detailLabel.get();
    auto detailScroll = CreateScope<UIScrollView>();
    detailScroll->SetPosition({8, 504}); detailScroll->SetSize({980, 360});
    m_Detail->SetPosition({0, 0});
    detailScroll->SetContent(std::move(detailLabel));
    content->AddChild(std::move(detailScroll));
    label("Counter: raw samples, latest 512; yellow = filtered Frame. No samples means uncollected, not zero.", 874);
    m_Counters = combo(8, 904, {}, [this](std::size_t, const std::string& name)
    { if (m_Updating == false) { auto f = m_Model.GetFilter(); f.CounterName = name; m_Model.SetFilter(std::move(f)); Refresh(); } });
    auto* language = combo(360, 904, {"English", "Japanese"}, [this](std::size_t index, const std::string&)
    {
        auto filter = m_Model.GetFilter();
        filter.Language = index == 1 ? Diagnostics::DiagnosticLanguage::Japanese : Diagnostics::DiagnosticLanguage::English;
        m_Model.SetFilter(std::move(filter)); Refresh();
    });
    language->SetName("DiagnosticsLanguage");
    auto graph = CreateScope<DiagnosticChart>(m_Model, false); graph->SetName("DiagnosticsCounterGraph"); graph->SetPosition({8, 940}); graph->SetSize({980, 170}); content->AddChild(std::move(graph));
    label("CPU timeline: horizontal = ns; one lane/thread, roots above children. Green=complete, orange=incomplete.", 1126);
    label("Display cap: 2048 intervals / 32 threads. Use Frame/Thread filters and measurement rows for scope details.", 1152);
    m_TimelineInfo = label("No CPU scopes collected", 1178, 100); m_TimelineInfo->SetWrapMode(UITextWrapMode::Word);
    auto timeline = CreateScope<DiagnosticChart>(m_Model, true); timeline->SetName("DiagnosticsTraceTimeline"); timeline->SetPosition({8, 1288}); timeline->SetSize({980, 480}); content->AddChild(std::move(timeline));
    scroll->SetContent(std::move(content)); window->AddChild(std::move(scroll));
    m_Root = context.AddRootChild(std::move(window));
    Update(1.0f);
    return true;
}

void DiagnosticsRavenPanel::Detach()
{
    // WidgetのCallback/ChartがModelを参照するため、先にWidget Treeを破棄します。
    if (m_Root != nullptr && m_Root->GetParent() != nullptr) { m_Root->GetParent()->RemoveChild(m_Root); }
    m_Root = nullptr; m_Status = nullptr; m_Detail = nullptr; m_TimelineInfo = nullptr; m_Table = nullptr; m_Counters = nullptr;
    m_Store.reset(); m_Model = DiagnosticsViewModel{};
    m_Elapsed = 1.0f; m_Frozen = false; m_FilterError.clear();
}
void DiagnosticsRavenPanel::SetVisible(bool visible) { if (m_Root != nullptr) { m_Root->SetVisible(visible); } }
bool DiagnosticsRavenPanel::IsAttached() const { return m_Root != nullptr; }
UIElement* DiagnosticsRavenPanel::GetRootElement() const { return m_Root; }
const DiagnosticsViewModel& DiagnosticsRavenPanel::GetModel() const { return m_Model; }
void DiagnosticsRavenPanel::Update(float deltaSeconds)
{
    if (m_Root == nullptr || m_Frozen == true) { return; }
    m_Elapsed += std::max(0.0f, deltaSeconds);
    if (m_Elapsed < SnapshotIntervalSeconds) { return; }
    m_Elapsed = 0;
    const auto* current = m_Model.GetSnapshot();
    const auto drops = Diagnostics::GetAsyncDroppedCount();
    const auto fallbacks = Diagnostics::GetValidationFallbackCount();
    const bool lossChanged = drops != m_Model.GetTimeline().DroppedEvents || fallbacks != m_Model.GetNativeFallbacks();
    auto snapshot = m_Store->TrySnapshot(current != nullptr && lossChanged == false ? current->Revision : 0);
    if (snapshot != nullptr)
    {
        m_Model.SetSnapshot(std::move(snapshot), drops, fallbacks); Refresh();
    }
}
std::string DiagnosticsRavenPanel::Cell(std::size_t row, std::size_t column) const
{
    if (row >= m_Model.GetRows().size() || m_Model.GetSnapshot() == nullptr) { return {}; }
    const auto& event = m_Model.GetSnapshot()->Events[m_Model.GetRows()[row]].Event;
    switch (column)
    {
    case 0: return DiagnosticsViewModel::SeverityName(event.Severity);
    case 1: return DiagnosticsViewModel::CategoryName(event.Category);
    case 2: { const auto frame = DiagnosticsViewModel::EventFrame(event); return frame.has_value() ? std::to_string(*frame) : "?"; }
    case 3: return std::to_string(event.ThreadId);
    default: return DiagnosticsViewModel::EventText(event, m_Model.GetFilter().Language);
    }
}
void DiagnosticsRavenPanel::Refresh()
{
    if (m_Table == nullptr) { return; }
    m_Updating = true;
    m_Table->NotifyDataSourceChanged();
    m_Table->SelectRow(m_Model.GetSelectedRow().value_or(UITable::NoSelection));
    if (m_Counters->GetOptions() != m_Model.GetCounterNames()) { m_Counters->SetOptions(m_Model.GetCounterNames()); }
    const auto& names = m_Model.GetCounterNames();
    const auto counter = std::find(names.begin(), names.end(), m_Model.GetFilter().CounterName);
    if (counter != names.end()) { m_Counters->SetSelectedIndex(static_cast<std::size_t>(counter - names.begin())); }
    m_Updating = false;
    const auto* snapshot = m_Model.GetSnapshot();
    if (snapshot == nullptr) { return; }
    const auto& timeline = m_Model.GetTimeline();
    std::ostringstream status;
    status << "Retained " << snapshot->Events.size() << '/' << snapshot->Capacity << " events, estimated bytes=" << snapshot->RetainedBytes
        << "; evicted=" << snapshot->Evicted << "; rejected=" << snapshot->Rejected << "; process queue drops=" << timeline.DroppedEvents
        << "; native stderr fallbacks=" << m_Model.GetNativeFallbacks() << "\nMissing begin/end/parent=" << timeline.MissingBegins << '/' << timeline.MissingEnds << '/' << timeline.MissingParents;
    const auto& graph = m_Model.GetCounterGraph();
    if (graph.empty() == false)
    {
        auto extrema = std::minmax_element(graph.begin(), graph.end(), [](const auto& a, const auto& b) { return a.Value < b.Value; });
        status << "; counter=" << graph.front().Name << " min/max=" << extrema.first->Value << '/' << extrema.second->Value;
    }
    if (m_FilterError.empty() == false) { status << "\n" << m_FilterError; }
    m_Status->SetText(status.str());
    std::ostringstream traceInfo;
    traceInfo << "Intervals=" << timeline.Intervals.size() << "; invalid=" << timeline.InvalidEvents << "; top-to-bottom Thread IDs:";
    std::uint64_t last = UINT64_MAX; std::size_t lanes = 0;
    std::uint64_t origin = UINT64_MAX, end = 0;
    for (const auto& interval : timeline.Intervals)
    {
        if (interval.ThreadId != last && lanes < MaxTimelineThreads) { traceInfo << ' ' << interval.ThreadId; last = interval.ThreadId; ++lanes; }
        origin = std::min(origin, interval.BeginNanoseconds == 0 ? interval.EndNanoseconds : interval.BeginNanoseconds);
        end = std::max(end, std::max(interval.BeginNanoseconds, interval.EndNanoseconds));
    }
    if (timeline.Intervals.empty() == false) { traceInfo << "\nTime axis(ns): " << origin << " ... " << end; }
    m_TimelineInfo->SetText(traceInfo.str());
    const auto* selected = m_Model.GetSelectedEvent();
    if (selected == nullptr) { m_Detail->SetText(m_Model.WasSelectionEvicted() ? "Selected event was evicted from bounded history." : "Select an event"); return; }
    const auto& event = selected->Event;
    const auto selectedFrame = DiagnosticsViewModel::EventFrame(event);
    std::ostringstream detail;
    detail << "Store ID=" << selected->Id << " Event ID=" << event.Id << " Sequence=" << event.Sequence << " Timestamp(ns)=" << event.TimestampNanoseconds
        << " Thread=" << event.ThreadId << "\n" << DiagnosticsViewModel::SeverityName(event.Severity) << ' '
        << DiagnosticsViewModel::CategoryName(event.Category) << " Frame=" << (selectedFrame.has_value() ? std::to_string(*selectedFrame) : "?")
        << "\n" << DiagnosticsViewModel::EventText(event, m_Model.GetFilter().Language);
    if (const auto* message = std::get_if<Diagnostics::DiagnosticMessage>(&event.Payload); message != nullptr)
    {
        detail << "\nDictionary=" << message->DictionaryVersion << " Schema=" << message->SchemaVersion
            << " Schema hash=" << message->SchemaHash << " Arguments=" << message->Arguments.size();
    }
    if (const auto* validation = std::get_if<Diagnostics::DiagnosticValidation>(&event.Payload); validation != nullptr)
    {
        detail << "\nNative severity=" << validation->NativeSeverity << " type=" << validation->NativeType << " name/message truncated=" << validation->NameTruncated << '/' << validation->MessageTruncated;
    }
    if (const auto* metric = std::get_if<Diagnostics::DiagnosticMetric>(&event.Payload); metric != nullptr)
    {
        detail << "\nFrame=" << metric->FrameId << " Scope=" << metric->ScopeId << " Parent=" << metric->ParentScopeId;
        for (const auto& interval : timeline.Intervals)
        {
            if (interval.ThreadId == event.ThreadId && interval.ScopeId == metric->ScopeId)
            { detail << " Duration(ns)=" << (interval.Complete == true ? std::to_string(interval.EndNanoseconds - interval.BeginNanoseconds) : "incomplete"); break; }
        }
    }
    const auto text = detail.str();
    // Native本文に多数の改行があっても、下のGraphへ重ねずInspector内で読めるようにします。
    const auto estimatedLines = std::count(text.begin(), text.end(), '\n') + text.size() / 65 + 2;
    const float detailHeight = std::max(360.0f, static_cast<float>(estimatedLines) * 22.0f);
    m_Detail->SetSize({950, detailHeight}); m_Detail->SetPreferredSize({950, detailHeight});
    m_Detail->SetText(text);
}
}
