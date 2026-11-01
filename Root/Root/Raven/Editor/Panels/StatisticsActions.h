#pragma once

#include "Raven/Editor/Panels/StatisticsSnapshot.h"

#include <string>
#include <map>
#include <iosfwd>

namespace Raven
{

struct StatisticsActionResult
{
    bool Succeeded = false;
    bool Enabled = false;
    std::string Message;
};

// 1つの計測Streamの完了Frameだけを集計します。未計測項目には別のSampleCountを持たせ、
// 0件という実測値と混同しません。全Frameを保存せず固定容量で保持します。
class StatisticsUIRecording
{
public:
    static constexpr std::size_t MetricCount = 12u;
    static constexpr std::size_t CppAllocationMetricIndex = 10u;
    void Start();
    void Stop();
    void AddSample(const StatisticsUISnapshot& sample);
    bool SaveCSV(const std::string& path, std::string& message) const;
    bool IsRecording() const { return m_Recording; }
    std::uint64_t GetSampleCount() const { return m_SampleCount; }
    const std::array<double, MetricCount>& GetAverages() const { return m_Averages; }
    const std::array<double, MetricCount>& GetMaxima() const { return m_Maxima; }
    static const char* GetMetricName(std::size_t index);
    std::uint64_t GetMetricSampleCount(std::size_t index) const;
    void WriteCSVRows(std::ostream& stream, const std::string& scope) const;

private:
    bool m_Recording = false;
    bool m_HasFrame = false;
    std::uint64_t m_LastFrameIndex = 0u;
    std::uint64_t m_SampleCount = 0u;
    std::array<double, MetricCount> m_Averages{};
    std::array<double, MetricCount> m_Maxima{};
    std::array<std::uint64_t, MetricCount> m_MetricSampleCounts{};
};

// Window Close後も当該Windowの集計を保持します。Window IDは再利用されません。
// 長時間のWindow生成で無制限に履歴を増やさず、上限超過分は全Window合計にだけ含めます。
class StatisticsUIRecordingSession final
{
public:
    static constexpr std::size_t MaxWindowStreams = 64u;
    void Start();
    void Stop();
    void AddSample(const StatisticsSnapshot& snapshot);
    bool SaveCSV(const std::string& path, std::string& message) const;
    bool IsRecording() const { return m_Total.IsRecording(); }
    std::uint64_t GetSampleCount() const { return m_Total.GetSampleCount(); }
    const StatisticsUIRecording& GetTotal() const { return m_Total; }
    const std::map<std::uint64_t, StatisticsUIRecording>& GetWindows() const { return m_Windows; }
    std::uint64_t GetOmittedWindowSamples() const { return m_OmittedWindowSamples; }

private:
    StatisticsUIRecording m_Total;
    std::map<std::uint64_t, StatisticsUIRecording> m_Windows;
    std::uint64_t m_OmittedWindowSamples = 0u;
    bool m_UsesWindowBatch = false;
};

// Statistics UIからCPU Profilerへ送る書き込み操作の唯一の入口です。
// PanelはProfiler参照を保持せず、表示は次に取得するStatisticsSnapshotへ再同期します。
StatisticsActionResult SetCPUProfilerEnabledAction(bool enabled);

} // namespace Raven
