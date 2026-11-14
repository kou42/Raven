#include "Raven/Diagnostics/MeasurementTimeline.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace Raven::Diagnostics
{
DiagnosticTimeline ReconstructMeasurementTimeline(std::span<const DiagnosticEvent> events,
    std::uint64_t droppedEvents, std::size_t maxEvents)
{
    DiagnosticTimeline result;
    result.DroppedEvents = droppedEvents;
    if (events.size() > maxEvents)
    {
        result.InputLimitExceeded = true;
        return result;
    }
    struct Pair
    {
        const DiagnosticEvent* Begin = nullptr;
        const DiagnosticEvent* End = nullptr;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, Pair> scopes;
    for (const auto& event : events)
    {
        const auto* metric = std::get_if<DiagnosticMetric>(&event.Payload);
        if (metric == nullptr)
        {
            continue;
        }
        if (metric->IsValid() == false || event.ThreadId == 0
            || static_cast<unsigned>(event.Category) >= DiagnosticCategoryCount)
        {
            ++result.InvalidEvents;
            continue;
        }
        if (metric->Kind == DiagnosticMetricKind::Counter)
        {
            result.Counters.push_back({ metric->FrameId, event.ThreadId, event.TimestampNanoseconds,
                event.Category, std::string(metric->GetName()), metric->Value });
            continue;
        }
        auto& pair = scopes[{ event.ThreadId, metric->ScopeId }];
        auto*& slot = metric->Kind == DiagnosticMetricKind::TraceBegin ? pair.Begin : pair.End;
        if (slot != nullptr)
        {
            ++result.InvalidEvents;
        }
        else
        {
            slot = &event;
        }
    }
    for (const auto& [key, pair] : scopes)
    {
        const auto& available = pair.Begin != nullptr ? *pair.Begin : *pair.End;
        const auto& metric = std::get<DiagnosticMetric>(available.Payload);
        DiagnosticTraceInterval interval;
        interval.FrameId = metric.FrameId;
        interval.ThreadId = key.first;
        interval.ScopeId = key.second;
        interval.ParentScopeId = metric.ParentScopeId;
        interval.Name = metric.GetName();
        interval.Category = available.Category;
        if (metric.ParentScopeId != 0)
        {
            const auto parent = scopes.find({ key.first, metric.ParentScopeId });
            if (parent == scopes.end() || parent->second.Begin == nullptr)
            {
                ++result.MissingParents;
            }
        }
        if (pair.Begin == nullptr)
        {
            ++result.MissingBegins;
        }
        else
        {
            interval.BeginNanoseconds = pair.Begin->TimestampNanoseconds;
        }
        if (pair.End == nullptr)
        {
            ++result.MissingEnds;
        }
        else
        {
            interval.EndNanoseconds = pair.End->TimestampNanoseconds;
        }
        if (pair.Begin != nullptr && pair.End != nullptr)
        {
            const auto& end = std::get<DiagnosticMetric>(pair.End->Payload);
            interval.Complete = end.FrameId == metric.FrameId && end.ParentScopeId == metric.ParentScopeId
                && end.GetName() == metric.GetName() && pair.End->Category == pair.Begin->Category
                && interval.EndNanoseconds >= interval.BeginNanoseconds
                && pair.End->Sequence > pair.Begin->Sequence;
            if (interval.Complete == false)
            {
                ++result.InvalidEvents;
            }
        }
        result.Intervals.push_back(std::move(interval));
    }
    std::sort(result.Intervals.begin(), result.Intervals.end(), [](const auto& left, const auto& right)
    {
        return std::tie(left.ThreadId, left.BeginNanoseconds, left.ScopeId)
            < std::tie(right.ThreadId, right.BeginNanoseconds, right.ScopeId);
    });
    std::sort(result.Counters.begin(), result.Counters.end(), [](const auto& left, const auto& right)
    {
        return std::tie(left.FrameId, left.ThreadId, left.TimestampNanoseconds)
            < std::tie(right.FrameId, right.ThreadId, right.TimestampNanoseconds);
    });
    return result;
}
}
