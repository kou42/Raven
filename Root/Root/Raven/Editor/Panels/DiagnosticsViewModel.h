#pragma once

#include "Raven/Diagnostics/DiagnosticEventStore.h"
#include "Raven/Diagnostics/MeasurementTimeline.h"

#include <optional>

namespace Raven
{
struct DiagnosticsViewFilter
{
    std::string Search;
    std::optional<Diagnostics::DiagnosticCategory> Category;
    Diagnostics::DiagnosticSeverity MinimumSeverity = Diagnostics::DiagnosticSeverity::Info;
    std::optional<std::uint64_t> Frame;
    std::optional<std::uint64_t> Thread;
    bool UnassociatedOnly = false;
    bool IncludeMeasurements = false;
    std::string CounterName;
};

// Main Thread専用。Snapshotは所有し、Scene/Worker/Widgetへの参照を持ちません。
class DiagnosticsViewModel final
{
public:
    void SetSnapshot(std::shared_ptr<const Diagnostics::DiagnosticStoreSnapshot> snapshot,
        std::uint64_t queueDrops = 0, std::uint64_t nativeFallbacks = 0);
    void SetFilter(DiagnosticsViewFilter filter);
    const DiagnosticsViewFilter& GetFilter() const noexcept;
    const std::vector<std::size_t>& GetRows() const noexcept;
    const Diagnostics::DiagnosticStoreSnapshot* GetSnapshot() const noexcept;
    const Diagnostics::DiagnosticTimeline& GetTimeline() const noexcept;
    const std::vector<std::string>& GetCounterNames() const noexcept;
    const std::vector<Diagnostics::DiagnosticCounterSample>& GetCounterGraph() const noexcept;
    void SelectRow(std::size_t row);
    void SelectEvent(std::uint64_t storeId);
    void ClearSelection() noexcept;
    const Diagnostics::DiagnosticStoredEvent* GetSelectedEvent() const noexcept;
    std::optional<std::size_t> GetSelectedRow() const;
    bool WasSelectionEvicted() const noexcept;
    std::uint64_t GetNativeFallbacks() const noexcept;
    static std::optional<std::uint64_t> EventFrame(const Diagnostics::DiagnosticEvent& event);
    static std::string EventText(const Diagnostics::DiagnosticEvent& event);
    static const char* CategoryName(Diagnostics::DiagnosticCategory category) noexcept;
    static const char* SeverityName(Diagnostics::DiagnosticSeverity severity) noexcept;

private:
    bool MatchesContext(const Diagnostics::DiagnosticEvent& event, bool includeFrame) const;
    void Rebuild();
    std::shared_ptr<const Diagnostics::DiagnosticStoreSnapshot> m_Snapshot;
    DiagnosticsViewFilter m_Filter;
    std::vector<std::size_t> m_Rows;
    Diagnostics::DiagnosticTimeline m_Timeline;
    std::vector<std::string> m_CounterNames;
    std::vector<Diagnostics::DiagnosticCounterSample> m_Graph;
    std::optional<std::uint64_t> m_SelectedId;
    std::uint64_t m_QueueDrops = 0;
    std::uint64_t m_NativeFallbacks = 0;
    bool m_SelectionEvicted = false;
};
}
