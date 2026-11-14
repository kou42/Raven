#pragma once

#include "Raven/Diagnostics/DiagnosticAPI.h"

#include <span>

namespace Raven::Diagnostics
{
struct DiagnosticCounterSample
{
    std::uint64_t FrameId = 0;
    std::uint64_t ThreadId = 0;
    std::uint64_t TimestampNanoseconds = 0;
    DiagnosticCategory Category = DiagnosticCategory::Runtime;
    std::string Name;
    double Value = 0.0;
};

struct DiagnosticTraceInterval
{
    std::uint64_t FrameId = 0;
    std::uint64_t ThreadId = 0;
    std::uint64_t ScopeId = 0;
    std::uint64_t ParentScopeId = 0;
    std::uint64_t BeginNanoseconds = 0;
    std::uint64_t EndNanoseconds = 0;
    DiagnosticCategory Category = DiagnosticCategory::Runtime;
    std::string Name;
    bool Complete = false;
};

struct DiagnosticTimeline
{
    std::vector<DiagnosticCounterSample> Counters;
    std::vector<DiagnosticTraceInterval> Intervals;
    std::uint64_t MissingBegins = 0;
    std::uint64_t MissingEnds = 0;
    std::uint64_t MissingParents = 0;
    std::uint64_t InvalidEvents = 0;
    // Queueの両端が共にDropされたScopeはイベントだけでは推測できません。
    // 捕捉時のDrop差分を別途渡し、全体の完全性を過大評価しないようにします。
    std::uint64_t DroppedEvents = 0;
    bool InputLimitExceeded = false;
};

// オフライン再構築。入力数を先に制限し、Thread/Scope別にBegin/Endを照合します。
// 出力区間はThread/開始時刻順。異なるThreadの到着順に依存しません。
DiagnosticTimeline ReconstructMeasurementTimeline(std::span<const DiagnosticEvent> events,
    std::uint64_t droppedEvents = 0, std::size_t maxEvents = 1000000);
}
