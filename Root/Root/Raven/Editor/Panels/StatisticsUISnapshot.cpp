#include "Raven/Editor/Panels/StatisticsSnapshot.h"
#include "Raven/UI/Core/UIContext.h"

#include <algorithm>
#include <cmath>

namespace Raven
{
StatisticsUISnapshot BuildStatisticsUISnapshot(const UIFrameMetrics& metrics)
{
    StatisticsUISnapshot result;
    result.FrameIndex = metrics.FrameIndex;
    result.Available = metrics.Available;
    result.BuildMilliseconds = metrics.BuildMilliseconds;
    result.DrawCommandCount = metrics.DrawCommandCount;
    result.VertexCount = metrics.VertexCount;
    result.IndexCount = metrics.IndexCount;
    result.TextureSwitchCount = metrics.TextureSwitchCount;
    result.FrameAllocationCount = metrics.FrameAllocationCount;
    result.FrameAllocatedBytes = metrics.FrameAllocatedBytes;
    result.RetainedAllocationBytes = metrics.RetainedAllocationBytes;
    result.ElementCount = metrics.ElementCount;
    result.PeakElementCount = metrics.PeakElementCount;
    result.CppAllocations = metrics.CppAllocations;
    return result;
}

void BuildStatisticsUIWindowSnapshots(const UICompletedFrameMetrics& completed,
    StatisticsSnapshot& snapshot)
{
    snapshot.UI = {};
    snapshot.UIWindows.clear();
    snapshot.UIAllWindows = {};
    snapshot.UIWindows.reserve(completed.Windows.size());
    if (completed.FrameIndex == 0u)
    {
        return;
    }
    auto& total = snapshot.UIAllWindows;
    total.FrameIndex = completed.FrameIndex;
    total.CppAllocations.Available = true;
    for (const auto& window : completed.Windows)
    {
        const auto& metrics = window.Metrics;
        if (metrics.Available == false || metrics.FrameIndex == 0u ||
            std::isfinite(metrics.BuildMilliseconds) == false || metrics.BuildMilliseconds < 0.0 ||
            std::any_of(snapshot.UIWindows.begin(), snapshot.UIWindows.end(),
                [&window](const StatisticsUIWindowSnapshot& existing) { return existing.WindowID == window.WindowID; }))
        {
            continue;
        }
        snapshot.UIWindows.push_back({window.WindowID, window.MainWindow, BuildStatisticsUISnapshot(metrics)});
        if (window.MainWindow == true)
        {
            snapshot.UI = snapshot.UIWindows.back().Metrics;
        }
        total.Available = true;
        total.BuildMilliseconds += metrics.BuildMilliseconds;
        total.DrawCommandCount += metrics.DrawCommandCount;
        total.VertexCount += metrics.VertexCount;
        total.IndexCount += metrics.IndexCount;
        // Window間ではTexture Bindingを共有しないため、Window別の切替数を加算します。
        total.TextureSwitchCount += metrics.TextureSwitchCount;
        total.FrameAllocationCount += metrics.FrameAllocationCount;
        total.FrameAllocatedBytes += metrics.FrameAllocatedBytes;
        total.RetainedAllocationBytes += metrics.RetainedAllocationBytes;
        total.ElementCount += metrics.ElementCount;
        total.CppAllocations.Available = total.CppAllocations.Available && metrics.CppAllocations.Available;
        total.CppAllocations.Count += metrics.CppAllocations.Count;
        total.CppAllocations.Bytes += metrics.CppAllocations.Bytes;
    }
    total.CppAllocations.Available = total.Available && total.CppAllocations.Available;
    total.PeakElementCount = std::max<std::uint64_t>(completed.PeakElementCount, total.ElementCount);
}
} // namespace Raven
