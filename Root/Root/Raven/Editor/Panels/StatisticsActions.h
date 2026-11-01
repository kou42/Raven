#pragma once

#include "Raven/Editor/Panels/StatisticsSnapshot.h"

#include <string>

namespace Raven
{

struct StatisticsActionResult
{
    bool Succeeded = false;
    bool Enabled = false;
    std::string Message;
};

// Main Window UIContextの完了Frameだけを集計します。補助Window/GPU時間や
// DrawList外のAllocationは含みません。全Frameを保存せず固定容量で保持します。
class StatisticsUIRecording
{
public:
    static constexpr std::size_t MetricCount = 10u;
    void Start();
    void Stop();
    void AddSample(const StatisticsUISnapshot& sample);
    bool SaveCSV(const std::string& path, std::string& message) const;
    bool IsRecording() const { return m_Recording; }
    std::uint64_t GetSampleCount() const { return m_SampleCount; }
    const std::array<double, MetricCount>& GetAverages() const { return m_Averages; }
    const std::array<double, MetricCount>& GetMaxima() const { return m_Maxima; }
    static const char* GetMetricName(std::size_t index);

private:
    bool m_Recording = false;
    bool m_HasFrame = false;
    std::uint64_t m_LastFrameIndex = 0u;
    std::uint64_t m_SampleCount = 0u;
    std::array<double, MetricCount> m_Averages{};
    std::array<double, MetricCount> m_Maxima{};
};

// Statistics UIからCPU Profilerへ送る書き込み操作の唯一の入口です。
// PanelはProfiler参照を保持せず、表示は次に取得するStatisticsSnapshotへ再同期します。
StatisticsActionResult SetCPUProfilerEnabledAction(bool enabled);

} // namespace Raven
