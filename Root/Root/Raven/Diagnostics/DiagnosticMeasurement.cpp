#include "Raven/Diagnostics/DiagnosticMeasurement.h"

#include <atomic>

namespace Raven::Diagnostics
{
namespace
{
std::atomic<bool> s_Measurements{ false };
std::atomic<std::uint64_t> s_Frame{ 0 };
std::atomic<std::uint64_t> s_Scope{ 0 };
thread_local std::uint64_t s_ParentScope = 0;
thread_local std::uint64_t s_ParentSession = 0;
}

void SetMeasurementsEnabled(bool enabled) noexcept
{
    s_Measurements.store(enabled, std::memory_order_relaxed);
}

bool IsMeasurementsEnabled() noexcept
{
    return RAVEN_ENABLE_DIAGNOSTICS != 0 && RAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS != 0
        && s_Measurements.load(std::memory_order_relaxed) && IsDiagnosticsEnabled();
}

std::uint64_t BeginDiagnosticFrame() noexcept
{
    return s_Frame.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::uint64_t GetDiagnosticFrameId() noexcept
{
    return s_Frame.load(std::memory_order_relaxed);
}

void RecordCounter(std::string_view name, double value, DiagnosticCategory category) noexcept
{
    if (IsMeasurementsEnabled() == false)
    {
        return;
    }
    DiagnosticMetric metric;
    metric.SetName(name);
    metric.Value = value;
    metric.FrameId = GetDiagnosticFrameId();
    EmitMetric(metric, category, GetDiagnosticSessionId());
}

TraceScope::TraceScope(std::string_view name, DiagnosticCategory category) noexcept
    : m_Category(category)
{
    if (IsMeasurementsEnabled() == false || name.empty() == true)
    {
        return;
    }
    m_SessionId = GetDiagnosticSessionId();
    if (s_ParentSession != m_SessionId)
    {
        s_ParentScope = 0;
        s_ParentSession = m_SessionId;
    }
    m_Metric.emplace();
    m_Metric->Kind = DiagnosticMetricKind::TraceBegin;
    m_Metric->FrameId = GetDiagnosticFrameId();
    m_Metric->ScopeId = s_Scope.fetch_add(1, std::memory_order_relaxed) + 1;
    m_Metric->ParentScopeId = s_ParentScope;
    m_Metric->SetName(name);
    m_Active = EmitMetric(*m_Metric, category, m_SessionId);
    if (m_Active == true)
    {
        s_ParentScope = m_Metric->ScopeId;
    }
}

TraceScope::~TraceScope()
{
    if (m_Active == true)
    {
        if (s_ParentSession == m_SessionId && s_ParentScope == m_Metric->ScopeId)
        {
            s_ParentScope = m_Metric->ParentScopeId;
        }
        m_Metric->Kind = DiagnosticMetricKind::TraceEnd;
        EmitMetric(*m_Metric, m_Category, m_SessionId);
    }
}

ProfiledTraceScope::ProfiledTraceScope(const char* name, DiagnosticCategory category)
    : m_Profile(name)
#if RAVEN_ENABLE_DIAGNOSTICS && RAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS
    , m_Trace(name != nullptr ? name : "Unnamed", category)
#endif
{
    (void)category;
}

ProfiledTraceScope::~ProfiledTraceScope() = default;
}
