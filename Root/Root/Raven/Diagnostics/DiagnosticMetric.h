#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Raven::Diagnostics
{
enum class DiagnosticMetricKind : std::uint8_t
{
    Counter = 1, TraceBegin = 2, TraceEnd = 3
};

constexpr std::size_t MaxDiagnosticMetricNameBytes = 96;

// Producer上の文字列確保を避け、非同期出力まで名前と数値を値で保持します。
// Counterはdouble（既存CPUProfilerと同じ精度）。Traceは開始FrameをEndにも保持します。
struct DiagnosticMetric
{
    DiagnosticMetricKind Kind = DiagnosticMetricKind::Counter;
    std::uint64_t FrameId = 0;
    std::uint64_t ScopeId = 0;
    std::uint64_t ParentScopeId = 0;
    double Value = 0.0;
    std::array<char, MaxDiagnosticMetricNameBytes> Name{};
    std::uint16_t NameBytes = 0;

    void SetName(std::string_view name) noexcept;
    std::string_view GetName() const noexcept;
    bool IsValid() const noexcept;
    bool operator==(const DiagnosticMetric&) const = default;
};
}
