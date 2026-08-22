#include "CPUProfiler.h"

#include <utility>

namespace Raven
{
namespace
{
// Scopeの入れ子深度はThreadごとに独立して管理します。
// Job System導入後に複数Workerが同時にProfilerを使用しても、別ThreadのDepthと干渉しません。
thread_local uint32_t s_CPUProfileDepth = 0;
}

CPUProfiler& CPUProfiler::Get()
{
    static CPUProfiler profiler;
    return profiler;
}

void CPUProfiler::BeginFrame()
{
    if (m_Enabled.load(std::memory_order_relaxed) == false)
    {
        return;
    }

    const Clock::time_point now = Clock::now();
    std::lock_guard<std::mutex> lock(m_ResultMutex);

    // 2frame目以降は、新frame開始時点を直前frameの終了時点として扱います。
    // ApplicationのRenderer::BeginFrame()と同じ境界を共有できるため、Profiler専用の
    // EndFrame呼び出しをApplicationへ追加せず、frame全体のCPU時間を測定できます。
    if (m_FrameActive)
    {
        m_WriteFrame.FrameTimeMilliseconds =
            std::chrono::duration<double, std::milli>(now - m_FrameStart).count();

        // 完成したframeを読み取り側へ公開します。
        // swap後のm_WriteFrameは古い読み取りbufferを再利用するため、毎frameのallocationを抑えます。
        std::swap(m_WriteFrame, m_LastFrame);
    }

    ++m_FrameIndex;
    m_FrameStart = now;
    m_FrameActive = true;
    m_WriteFrame.FrameIndex = m_FrameIndex;
    m_WriteFrame.FrameTimeMilliseconds = 0.0;
    m_WriteFrame.Results.clear();
}

void CPUProfiler::AddResult(CPUProfileResult result)
{
    if (m_Enabled.load(std::memory_order_relaxed) == false)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_ResultMutex);

    // BeginFrame前の計測結果はframeへ所属させられないため記録しません。
    if (m_FrameActive == false)
    {
        return;
    }

    m_WriteFrame.Results.push_back(std::move(result));
}

const CPUProfileFrame& CPUProfiler::GetLastFrame() const
{
    return m_LastFrame;
}

void CPUProfiler::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_ResultMutex);
    m_Enabled.store(enabled, std::memory_order_relaxed);
    m_FrameActive = false;

    m_WriteFrame.Results.clear();
    m_LastFrame.Results.clear();
    m_WriteFrame.FrameTimeMilliseconds = 0.0;
    m_LastFrame.FrameTimeMilliseconds = 0.0;
}

bool CPUProfiler::IsEnabled() const
{
    return m_Enabled.load(std::memory_order_relaxed);
}

CPUProfileScope::CPUProfileScope(const char* name)
    : m_Name(name)
{
    if (CPUProfiler::Get().IsEnabled() == false)
    {
        return;
    }

    m_Depth = s_CPUProfileDepth;
    ++s_CPUProfileDepth;
    m_Start = Clock::now();
    m_Active = true;
}

CPUProfileScope::~CPUProfileScope()
{
    if (m_Active == false)
    {
        return;
    }

    const Clock::time_point end = Clock::now();

    if (s_CPUProfileDepth > 0)
    {
        --s_CPUProfileDepth;
    }

    CPUProfileResult result{};
    result.Name = m_Name != nullptr ? m_Name : "Unnamed";
    result.DurationMilliseconds =
        std::chrono::duration<double, std::milli>(end - m_Start).count();
    result.ThreadId = std::this_thread::get_id();
    result.Depth = m_Depth;

    CPUProfiler::Get().AddResult(std::move(result));
}

} // namespace Raven
