#pragma once

#include <string>

namespace Raven
{

struct StatisticsActionResult
{
    bool Succeeded = false;
    bool Enabled = false;
    std::string Message;
};

// Statistics UIからCPU Profilerへ送る書き込み操作の唯一の入口です。
// PanelはProfiler参照を保持せず、表示は次に取得するStatisticsSnapshotへ再同期します。
StatisticsActionResult SetCPUProfilerEnabledAction(bool enabled);

} // namespace Raven
