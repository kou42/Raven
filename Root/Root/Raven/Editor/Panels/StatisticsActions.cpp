#include "Raven/Editor/Panels/StatisticsActions.h"

#include "Raven/Core/CPUProfiler.h"

namespace Raven
{

StatisticsActionResult SetCPUProfilerEnabledAction(bool enabled)
{
    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.SetEnabled(enabled);
    const bool applied = profiler.IsEnabled();
    return {
        applied == enabled,
        applied,
        applied == enabled
            ? (applied == true ? "CPU Profiler enabled." : "CPU Profiler disabled.")
            : "Failed to change CPU Profiler state."
    };
}

} // namespace Raven
