#pragma once

#include "Raven/Diagnostics/DiagnosticAPI.h"
#include "Raven/Core/CPUProfiler.h"
#include <optional>

namespace Raven::Diagnostics
{
// 計測は既定で無効。Session開始とは独立し、明示的な診断時だけ有効化します。
void SetMeasurementsEnabled(bool enabled) noexcept;
bool IsMeasurementsEnabled() noexcept;
std::uint64_t BeginDiagnosticFrame() noexcept;
std::uint64_t GetDiagnosticFrameId() noexcept;
void RecordCounter(std::string_view name, double value,
    DiagnosticCategory category = DiagnosticCategory::Runtime) noexcept;

class TraceScope final
{
public:
    explicit TraceScope(std::string_view name, DiagnosticCategory category = DiagnosticCategory::Runtime) noexcept;
    ~TraceScope();
    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;

private:
    // 無効時は大きな固定Payloadを初期化せず、Scope自体の副作用だけを残さないようにします。
    std::optional<DiagnosticMetric> m_Metric;
    DiagnosticCategory m_Category;
    std::uint64_t m_SessionId = 0;
    bool m_Active = false;
};

// 接続点だけで両Profilerへ記録するAdapter。CoreのCPUProfilerはDiagnosticsを参照しません。
// nameは既存CPUProfileScopeが借用するため、このAdapterの終了まで生存させてください。
class ProfiledTraceScope final
{
public:
    ProfiledTraceScope(const char* name, DiagnosticCategory category);
    ~ProfiledTraceScope();
    ProfiledTraceScope(const ProfiledTraceScope&) = delete;
    ProfiledTraceScope& operator=(const ProfiledTraceScope&) = delete;
private:
    CPUProfileScope m_Profile;
#if RAVEN_ENABLE_DIAGNOSTICS && RAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS
    TraceScope m_Trace;
#endif
};
}

#if RAVEN_ENABLE_DIAGNOSTICS && RAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS
#define RAVEN_COUNTER(name, value) do { if (::Raven::Diagnostics::IsMeasurementsEnabled() == true) { \
    ::Raven::Diagnostics::RecordCounter((name), (value)); } } while (false)
#define RAVEN_TRACE_SCOPE(name) ::Raven::Diagnostics::TraceScope \
    RAVEN_PROFILE_CONCAT(ravenTrace_, __LINE__)(::Raven::Diagnostics::IsMeasurementsEnabled() == true ? (name) : std::string_view{})
#else
#define RAVEN_COUNTER(name, value) do {} while (false)
#define RAVEN_TRACE_SCOPE(name) do {} while (false)
#endif
