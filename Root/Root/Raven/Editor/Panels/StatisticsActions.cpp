#include "Raven/Editor/Panels/StatisticsActions.h"

#include "Raven/Core/CPUProfiler.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>

namespace Raven
{

void StatisticsUIRecording::Start()
{
    // 再開始は新しいセッションです。Stop後の結果は次のStartまで保持します。
    *this = StatisticsUIRecording{};
    m_Recording = true;
}

void StatisticsUIRecording::Stop()
{
    m_Recording = false;
}

const char* StatisticsUIRecording::GetMetricName(std::size_t index)
{
    static constexpr const char* names[MetricCount] = {
        "build_ms", "draw_commands", "vertices", "indices", "texture_switches",
        "drawlist_allocations", "drawlist_allocated_bytes", "drawlist_retained_bytes",
        "elements", "lifetime_peak_elements"
    };
    return index < MetricCount ? names[index] : "";
}

void StatisticsUIRecording::AddSample(const StatisticsUISnapshot& sample)
{
    if (m_Recording == false || sample.Available == false ||
        std::isfinite(sample.BuildMilliseconds) == false || sample.BuildMilliseconds < 0.0 ||
        (m_HasFrame == true && sample.FrameIndex <= m_LastFrameIndex))
    {
        return;
    }
    const std::array<double, MetricCount> values = {
        sample.BuildMilliseconds, static_cast<double>(sample.DrawCommandCount),
        static_cast<double>(sample.VertexCount), static_cast<double>(sample.IndexCount),
        static_cast<double>(sample.TextureSwitchCount), static_cast<double>(sample.FrameAllocationCount),
        static_cast<double>(sample.FrameAllocatedBytes), static_cast<double>(sample.RetainedAllocationBytes),
        static_cast<double>(sample.ElementCount), static_cast<double>(sample.PeakElementCount)
    };
    m_HasFrame = true;
    m_LastFrameIndex = sample.FrameIndex;
    ++m_SampleCount;
    for (std::size_t index = 0u; index < MetricCount; ++index)
    {
        // 総和を蓄積せず逐次平均を使い、長時間記録の保持量を一定にします。
        m_Averages[index] += (values[index] - m_Averages[index]) / static_cast<double>(m_SampleCount);
        m_Maxima[index] = std::max(m_Maxima[index], values[index]);
    }
}

bool StatisticsUIRecording::SaveCSV(const std::string& path, std::string& message) const
{
    if (m_Recording == true || m_SampleCount == 0u)
    {
        message = "Stop a non-empty recording before saving.";
        return false;
    }
    std::error_code error;
    if (std::filesystem::exists(path, error) == true || error)
    {
        message = "Cannot save: output exists or path is inaccessible.";
        return false;
    }
    std::ofstream stream(path);
    stream.imbue(std::locale::classic());
    stream << "scope,metric,samples,average,max\n" << std::setprecision(17);
    for (std::size_t index = 0u; index < MetricCount; ++index)
    {
        stream << "main_window_ui," << GetMetricName(index) << ',' << m_SampleCount << ','
            << m_Averages[index] << ',' << m_Maxima[index] << '\n';
    }
    stream.close();
    if (stream.fail() == true)
    {
        message = "Failed to write recording CSV.";
        return false;
    }
    message = "Saved: " + path;
    return true;
}

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
