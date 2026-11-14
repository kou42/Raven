#include "Raven/Editor/Panels/DiagnosticsViewModel.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace Raven
{
using namespace Diagnostics;
void DiagnosticsViewModel::SetSnapshot(std::shared_ptr<const DiagnosticStoreSnapshot> snapshot,
    std::uint64_t queueDrops, std::uint64_t nativeFallbacks)
{
    m_Snapshot = std::move(snapshot);
    m_QueueDrops = queueDrops;
    m_NativeFallbacks = nativeFallbacks;
    if (m_SelectedId.has_value() == true && GetSelectedEvent() == nullptr)
    {
        m_SelectedId.reset();
        m_SelectionEvicted = true;
    }
    Rebuild();
}
void DiagnosticsViewModel::SetFilter(DiagnosticsViewFilter filter)
{
    filter.Search.resize(std::min<std::size_t>(filter.Search.size(), 256));
    m_Filter = std::move(filter);
    Rebuild();
}
const DiagnosticsViewFilter& DiagnosticsViewModel::GetFilter() const noexcept { return m_Filter; }
const std::vector<std::size_t>& DiagnosticsViewModel::GetRows() const noexcept { return m_Rows; }
const DiagnosticStoreSnapshot* DiagnosticsViewModel::GetSnapshot() const noexcept { return m_Snapshot.get(); }
const DiagnosticTimeline& DiagnosticsViewModel::GetTimeline() const noexcept { return m_Timeline; }
const std::vector<std::string>& DiagnosticsViewModel::GetCounterNames() const noexcept { return m_CounterNames; }
const std::vector<DiagnosticCounterSample>& DiagnosticsViewModel::GetCounterGraph() const noexcept { return m_Graph; }
std::uint64_t DiagnosticsViewModel::GetNativeFallbacks() const noexcept { return m_NativeFallbacks; }

std::optional<std::uint64_t> DiagnosticsViewModel::EventFrame(const DiagnosticEvent& event)
{
    if (const auto* metric = std::get_if<DiagnosticMetric>(&event.Payload); metric != nullptr) { return metric->FrameId; }
    if (const auto* validation = std::get_if<DiagnosticValidation>(&event.Payload); validation != nullptr) { return validation->FrameId; }
    return std::nullopt;
}
const char* DiagnosticsViewModel::CategoryName(DiagnosticCategory category) noexcept
{
    switch (category)
    {
    case DiagnosticCategory::Core: return "Core";
    case DiagnosticCategory::RHI: return "RHI";
    case DiagnosticCategory::Renderer: return "Renderer";
    case DiagnosticCategory::Physics: return "Physics";
    case DiagnosticCategory::Animation: return "Animation";
    case DiagnosticCategory::Asset: return "Asset";
    case DiagnosticCategory::Runtime: return "Runtime";
    case DiagnosticCategory::SoftBody: return "SoftBody";
    }
    return "Unknown";
}
const char* DiagnosticsViewModel::SeverityName(DiagnosticSeverity severity) noexcept
{
    switch (severity)
    {
    case DiagnosticSeverity::Info: return "Info";
    case DiagnosticSeverity::Warning: return "Warning";
    case DiagnosticSeverity::Error: return "Error";
    }
    return "Unknown";
}
std::string DiagnosticsViewModel::EventText(const DiagnosticEvent& event)
{
    if (const auto* validation = std::get_if<DiagnosticValidation>(&event.Payload); validation != nullptr)
    {
        return std::string(validation->Backend == DiagnosticValidationBackend::Vulkan ? "Vulkan " : "DX12 ")
            + validation->Name + " [native ID " + std::to_string(validation->MessageId) + "] " + validation->Message;
    }
    if (const auto* metric = std::get_if<DiagnosticMetric>(&event.Payload); metric != nullptr)
    {
        std::ostringstream text;
        text << metric->GetName() << " = " << metric->Value;
        if (metric->Kind != DiagnosticMetricKind::Counter)
        {
            text << (metric->Kind == DiagnosticMetricKind::TraceBegin ? " [Begin " : " [End ") << metric->ScopeId << ']';
        }
        return text.str();
    }
    if (const auto* text = std::get_if<std::string>(&event.Payload); text != nullptr) { return *text; }
    if (const auto* integer = std::get_if<std::int64_t>(&event.Payload); integer != nullptr) { return std::to_string(*integer); }
    if (const auto* real = std::get_if<double>(&event.Payload); real != nullptr) { return std::to_string(*real); }
    return std::get<bool>(event.Payload) == true ? "true" : "false";
}
bool DiagnosticsViewModel::MatchesContext(const DiagnosticEvent& event, bool includeFrame) const
{
    if (m_Filter.Category.has_value() == true && event.Category != *m_Filter.Category) { return false; }
    if (m_Filter.Thread.has_value() == true && event.ThreadId != *m_Filter.Thread) { return false; }
    if (includeFrame == true)
    {
        const auto frame = EventFrame(event);
        if (m_Filter.UnassociatedOnly == true && frame.has_value() == true) { return false; }
        if (m_Filter.Frame.has_value() == true && frame != m_Filter.Frame) { return false; }
    }
    return true;
}
void DiagnosticsViewModel::Rebuild()
{
    m_Rows.clear();
    m_Graph.clear();
    m_CounterNames.clear();
    m_Timeline = {};
    if (m_Snapshot == nullptr) { return; }
    std::set<std::string> counterNames;
    std::vector<DiagnosticEvent> metrics;
    metrics.reserve(m_Snapshot->Events.size());
    for (std::size_t index = 0; index < m_Snapshot->Events.size(); ++index)
    {
        const auto& event = m_Snapshot->Events[index].Event;
        const auto* metric = std::get_if<DiagnosticMetric>(&event.Payload);
        if (MatchesContext(event, false) == true && metric != nullptr && metric->Kind == DiagnosticMetricKind::Counter)
        {
            counterNames.emplace(metric->GetName());
        }
        if (MatchesContext(event, true) == false) { continue; }
        if (metric != nullptr) { metrics.push_back(event); }
        if (metric != nullptr && m_Filter.IncludeMeasurements == false) { continue; }
        // Severityと本文検索はLog Tableだけに作用し、同FrameのPhysics Timelineを消しません。
        if (event.Severity < m_Filter.MinimumSeverity) { continue; }
        if (m_Filter.Search.empty() == false && EventText(event).find(m_Filter.Search) == std::string::npos) { continue; }
        m_Rows.push_back(index);
    }
    m_CounterNames.assign(counterNames.begin(), counterNames.end());
    if (counterNames.contains(m_Filter.CounterName) == false && m_CounterNames.empty() == false)
    {
        m_Filter.CounterName = m_CounterNames.front();
    }
    // GraphはFrame履歴を保ち、選択Frameは表示側のマーカーで示します。
    for (const auto& entry : m_Snapshot->Events)
    {
        const auto* metric = std::get_if<DiagnosticMetric>(&entry.Event.Payload);
        if (metric != nullptr && metric->Kind == DiagnosticMetricKind::Counter
            && metric->GetName() == m_Filter.CounterName && MatchesContext(entry.Event, false) == true)
        {
            m_Graph.push_back({ metric->FrameId, entry.Event.ThreadId, entry.Event.TimestampNanoseconds,
                entry.Event.Category, std::string(metric->GetName()), metric->Value });
        }
    }
    std::sort(m_Graph.begin(), m_Graph.end(), [](const auto& a, const auto& b) { return a.TimestampNanoseconds < b.TimestampNanoseconds; });
    constexpr std::size_t GraphLimit = 512;
    if (m_Graph.size() > GraphLimit) { m_Graph.erase(m_Graph.begin(), m_Graph.end() - GraphLimit); }
    m_Timeline = ReconstructMeasurementTimeline(metrics, m_QueueDrops);
}
void DiagnosticsViewModel::SelectRow(std::size_t row)
{
    if (m_Snapshot != nullptr && row < m_Rows.size()) { SelectEvent(m_Snapshot->Events[m_Rows[row]].Id); }
    else { ClearSelection(); }
}
void DiagnosticsViewModel::SelectEvent(std::uint64_t storeId)
{
    m_SelectedId = storeId;
    m_SelectionEvicted = false;
    if (GetSelectedEvent() == nullptr) { m_SelectedId.reset(); }
}
void DiagnosticsViewModel::ClearSelection() noexcept { m_SelectedId.reset(); m_SelectionEvicted = false; }
bool DiagnosticsViewModel::WasSelectionEvicted() const noexcept { return m_SelectionEvicted; }
const DiagnosticStoredEvent* DiagnosticsViewModel::GetSelectedEvent() const noexcept
{
    if (m_Snapshot == nullptr || m_SelectedId.has_value() == false) { return nullptr; }
    const auto it = std::find_if(m_Snapshot->Events.begin(), m_Snapshot->Events.end(),
        [this](const auto& entry) { return entry.Id == *m_SelectedId; });
    return it != m_Snapshot->Events.end() ? &*it : nullptr;
}
std::optional<std::size_t> DiagnosticsViewModel::GetSelectedRow() const
{
    if (m_Snapshot == nullptr || m_SelectedId.has_value() == false) { return std::nullopt; }
    for (std::size_t row = 0; row < m_Rows.size(); ++row)
    {
        if (m_Snapshot->Events[m_Rows[row]].Id == *m_SelectedId) { return row; }
    }
    return std::nullopt;
}
}
