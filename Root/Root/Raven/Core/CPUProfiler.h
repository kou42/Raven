#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Raven
{

// CPU上で実行された1つの計測区間を表します。
// 将来Job Systemを導入した際にもWorker Threadごとの実行時間を追跡できるよう、
// ThreadIdを最初から保持する構造にしています。
struct CPUProfileResult
{
    std::string Name;
    double DurationMilliseconds = 0.0;
    std::thread::id ThreadId{};
    uint32_t Depth = 0;
};

// 1 Application frame分のCPU計測結果を保持します。
// Front/Back Buffer方式でProfiler内部の書き込み中データとUI参照データを分離します。
struct CPUProfileFrame
{
    uint64_t FrameIndex = 0;
    double FrameTimeMilliseconds = 0.0;
    std::vector<CPUProfileResult> Results;
};

class CPUProfiler
{
public:
    static CPUProfiler& Get();

    // 新しいApplication frameを開始します。
    // 2frame目以降は、この呼び出し時に直前frameを自動的に読み取り用bufferへ公開します。
    // Renderer::BeginFrame()と同じ境界を利用できるため、Application側へEndFrame処理を増やさずに済みます。
    void BeginFrame();

    void AddResult(CPUProfileResult result);

    const CPUProfileFrame& GetLastFrame() const;

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

private:
    CPUProfiler() = default;

private:
    using Clock = std::chrono::steady_clock;

    // UIのMain Threadから切り替えながら、将来Worker Thread側がIsEnabled()を読む構成でも
    // データ競合しないようatomicで保持します。計測データ本体はm_ResultMutexで保護します。
    std::atomic<bool> m_Enabled{ true };
    bool m_FrameActive = false;
    uint64_t m_FrameIndex = 0;
    Clock::time_point m_FrameStart{};

    CPUProfileFrame m_WriteFrame{};
    CPUProfileFrame m_LastFrame{};

    // 現段階では主にMain Threadから計測しますが、次段階のJob Systemで
    // Worker Threadから同時にAddResult()されても壊れないように同期を入れています。
    mutable std::mutex m_ResultMutex;
};

// Scopeに入った時刻と抜けた時刻の差を自動記録するRAII Timerです。
// return / continue / 例外などでScopeを抜けてもDestructorが必ず計測を閉じるため、
// 手動Start/Stopより計測漏れを起こしにくい設計です。
class CPUProfileScope
{
public:
    explicit CPUProfileScope(const char* name);
    ~CPUProfileScope();

    CPUProfileScope(const CPUProfileScope&) = delete;
    CPUProfileScope& operator=(const CPUProfileScope&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    const char* m_Name = nullptr;
    Clock::time_point m_Start{};
    uint32_t m_Depth = 0;
    bool m_Active = false;
};

} // namespace Raven

// 同一Scope内で複数回使用しても変数名が衝突しないよう、__LINE__を連結します。
#define RAVEN_PROFILE_CONCAT_INTERNAL(a, b) a##b
#define RAVEN_PROFILE_CONCAT(a, b) RAVEN_PROFILE_CONCAT_INTERNAL(a, b)
#define RAVEN_PROFILE_SCOPE(name) \
    ::Raven::CPUProfileScope RAVEN_PROFILE_CONCAT(ravenCPUProfileScope_, __LINE__)(name)
#define RAVEN_PROFILE_FUNCTION() RAVEN_PROFILE_SCOPE(__FUNCTION__)
