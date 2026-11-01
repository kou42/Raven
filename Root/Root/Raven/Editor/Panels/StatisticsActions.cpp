#include "Raven/Editor/Panels/StatisticsActions.h"

#include "Raven/Core/CPUProfiler.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <functional>

namespace Raven
{

namespace
{
bool SaveRecordingCSV(const std::string& path, std::string& message,
    bool recording, std::uint64_t sampleCount, const std::function<void(std::ostream&)>& writeRows)
{
    if (recording == true || sampleCount == 0u)
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
    writeRows(stream);
    stream.close();
    if (stream.fail() == true)
    {
        message = "Failed to write recording CSV.";
        return false;
    }
    message = "Saved: " + path;
    return true;
}
} // namespace

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
        "elements", "lifetime_peak_elements", "cpp_allocations", "cpp_allocated_bytes"
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
        static_cast<double>(sample.ElementCount), static_cast<double>(sample.PeakElementCount),
        static_cast<double>(sample.CppAllocations.Count), static_cast<double>(sample.CppAllocations.Bytes)
    };
    m_HasFrame = true;
    m_LastFrameIndex = sample.FrameIndex;
    ++m_SampleCount;
    for (std::size_t index = 0u; index < MetricCount; ++index)
    {
        if (index >= CppAllocationMetricIndex && sample.CppAllocations.Available == false)
        {
            continue;
        }
        ++m_MetricSampleCounts[index];
        // 未計測Frameを平均の分母に含めず、計測済み0件のFrameは通常サンプルとして扱います。
        m_Averages[index] += (values[index] - m_Averages[index]) / static_cast<double>(m_MetricSampleCounts[index]);
        m_Maxima[index] = std::max(m_Maxima[index], values[index]);
    }
}

std::uint64_t StatisticsUIRecording::GetMetricSampleCount(std::size_t index) const
{
    return index < MetricCount ? m_MetricSampleCounts[index] : 0u;
}

void StatisticsUIRecording::WriteCSVRows(std::ostream& stream, const std::string& scope) const
{
    for (std::size_t index = 0u; index < MetricCount; ++index)
    {
        stream << scope << ',' << GetMetricName(index) << ',' << m_MetricSampleCounts[index] << ',';
        if (m_MetricSampleCounts[index] == 0u)
        {
            stream << "N/A,N/A\n";
        }
        else
        {
            stream << m_Averages[index] << ',' << m_Maxima[index] << '\n';
        }
    }
}

bool StatisticsUIRecording::SaveCSV(const std::string& path, std::string& message) const
{
    return SaveRecordingCSV(path, message, m_Recording, m_SampleCount,
        [this](std::ostream& stream) { WriteCSVRows(stream, "main_window_ui"); });
}

void StatisticsUIRecordingSession::Start()
{
    m_Total.Start();
    m_Windows.clear();
    m_OmittedWindowSamples = 0u;
    m_UsesWindowBatch = false;
}

void StatisticsUIRecordingSession::Stop()
{
    m_Total.Stop();
    for (auto& window : m_Windows)
    {
        window.second.Stop();
    }
}

void StatisticsUIRecordingSession::AddSample(const StatisticsSnapshot& snapshot)
{
    const auto previousCount = m_Total.GetSampleCount();
    if (snapshot.UIAllWindows.Available == true)
    {
        m_Total.AddSample(snapshot.UIAllWindows);
    }
    else if (m_UsesWindowBatch == false)
    {
        m_Total.AddSample(snapshot.UI);
    }
    if (m_Total.GetSampleCount() == previousCount)
    {
        return;
    }
    m_UsesWindowBatch = snapshot.UIAllWindows.Available;
    for (const auto& sample : snapshot.UIWindows)
    {
        auto found = m_Windows.find(sample.WindowID);
        if (found == m_Windows.end())
        {
            if (m_Windows.size() >= MaxWindowStreams)
            {
                ++m_OmittedWindowSamples;
                continue;
            }
            found = m_Windows.try_emplace(sample.WindowID).first;
            found->second.Start();
        }
        found->second.AddSample(sample.Metrics);
    }
}

bool StatisticsUIRecordingSession::SaveCSV(const std::string& path, std::string& message) const
{
    return SaveRecordingCSV(path, message, IsRecording(), GetSampleCount(),
        [this](std::ostream& stream)
        {
            m_Total.WriteCSVRows(stream, m_UsesWindowBatch == true ? "all_windows_ui" : "main_window_ui");
            for (const auto& window : m_Windows)
            {
                window.second.WriteCSVRows(stream, "window_" + std::to_string(window.first));
            }
            stream << "recording,omitted_window_samples,1," << m_OmittedWindowSamples << ','
                << m_OmittedWindowSamples << '\n';
        });
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
