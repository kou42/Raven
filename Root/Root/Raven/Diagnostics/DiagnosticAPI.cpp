#include "Raven/Diagnostics/DiagnosticAPI.h"
#include "Raven/Diagnostics/DiagnosticCollector.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace Raven
{
namespace Diagnostics
{
namespace
{
std::atomic<std::uint64_t> s_Sequence{ 0 };
std::atomic<std::uint64_t> s_NextThreadId{ 0 };
std::atomic<bool> s_Enabled{ true };
std::mutex s_ConsoleMutex;
std::mutex s_AsyncMutex;
std::shared_ptr<DiagnosticCollector> s_Collector;
std::uint64_t s_Generation = 0;
std::uint64_t s_TotalDropped = 0;
std::uint64_t s_SinkFailures = 0;
struct ThreadBufferState
{
    std::uint64_t ThreadId = 0;
    std::uint64_t Generation = 0;
    std::shared_ptr<EventBuffer> Buffer;
    ~ThreadBufferState()
    {
        if (Buffer != nullptr)
        {
            Buffer->Close();
        }
    }
};
thread_local ThreadBufferState s_ThreadBuffer;

const char* CategoryName(DiagnosticCategory category)
{
    switch (category)
    {
    case DiagnosticCategory::Core: return "Core";
    case DiagnosticCategory::RHI: return "RHI";
    case DiagnosticCategory::Renderer: return "Renderer";
    case DiagnosticCategory::Physics: return "Physics";
    case DiagnosticCategory::Animation: return "Animation";
    case DiagnosticCategory::Asset: return "Asset";
    case DiagnosticCategory::Runtime: return "Runtime";
    }
    return "Unknown";
}

const char* SeverityName(DiagnosticSeverity severity)
{
    switch (severity)
    {
    case DiagnosticSeverity::Info: return "INFO";
    case DiagnosticSeverity::Warning: return "WARN";
    case DiagnosticSeverity::Error: return "ERROR";
    }
    return "UNKNOWN";
}

// UTF-8の継続バイトで切断しないよう、上限を超えた末尾を除外します。
void LimitText(std::string& text)
{
    if (text.size() <= MaxDiagnosticTextBytes)
    {
        return;
    }
    std::size_t end = MaxDiagnosticTextBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
    {
        --end;
    }
    text.resize(end);
}
}

void NormalizePayload(DiagnosticPayload& payload)
{
    if (auto* text = std::get_if<std::string>(&payload); text != nullptr)
    {
        LimitText(*text);
    }
}

void WriteConsoleEvent(std::ostream& output, const DiagnosticEvent& event)
{
    output << "[Diagnostics][" << SeverityName(event.Severity) << "]["
        << CategoryName(event.Category) << "] id=" << event.Id
        << " seq=" << event.Sequence << " ts_ns=" << event.TimestampNanoseconds
        << " thread=" << event.ThreadId << " payload=";
    if (const auto* metric = std::get_if<DiagnosticMetric>(&event.Payload); metric != nullptr)
    {
        output << "metric=" << static_cast<unsigned>(metric->Kind) << " frame=" << metric->FrameId
            << " scope=" << metric->ScopeId << " parent=" << metric->ParentScopeId
            << " name=" << metric->GetName() << " value=" << metric->Value;
    }
    else
    {
        // 計測型は上で処理し、従来Logの書式は維持します。
        std::visit([&output](const auto& value)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, DiagnosticMetric> == false)
            {
                output << value;
            }
        }, event.Payload);
    }
    output << '\n';
}

bool StartAsyncLogging()
{
    if (RAVEN_ENABLE_DIAGNOSTICS == 0)
    {
        return false;
    }
    std::vector<std::unique_ptr<IDiagnosticSink>> sinks;
    sinks.push_back(std::make_unique<ConsoleSink>(std::cout));
    return StartAsyncLogging(std::move(sinks));
}

bool StartAsyncLogging(std::vector<std::unique_ptr<IDiagnosticSink>> sinks)
{
    if (RAVEN_ENABLE_DIAGNOSTICS == 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_AsyncMutex);
    if (s_Collector != nullptr || sinks.empty() == true)
    {
        return false;
    }
    for (const auto& sink : sinks)
    {
        if (sink == nullptr)
        {
            return false;
        }
    }
    try
    {
        auto collector = std::make_shared<DiagnosticCollector>(std::move(sinks));
        if (collector->Start() == false)
        {
            return false;
        }
        ++s_Generation;
        s_Collector = std::move(collector);
    }
    catch (...)
    {
        WriteEmergencyDiagnostic("Diagnostics worker unavailable; synchronous console remains active.");
        return false;
    }
    return true;
}

void FlushDiagnostics()
{
    std::lock_guard<std::mutex> lock(s_AsyncMutex);
    if (s_Collector != nullptr)
    {
        s_Collector->Flush();
    }
    else
    {
        std::lock_guard<std::mutex> consoleLock(s_ConsoleMutex);
        std::cout.flush();
    }
}

void StopAsyncLogging()
{
    std::lock_guard<std::mutex> lock(s_AsyncMutex);
    if (s_Collector == nullptr)
    {
        return;
    }
    s_Collector->Stop();
    s_SinkFailures += s_Collector->GetSinkFailureCount();
    s_Collector.reset();
    if (s_ThreadBuffer.Buffer != nullptr)
    {
        s_ThreadBuffer.Buffer->Close();
        s_ThreadBuffer.Buffer.reset();
        s_ThreadBuffer.Generation = 0;
    }
}

std::uint64_t GetAsyncDroppedCount()
{
    std::lock_guard<std::mutex> lock(s_AsyncMutex);
    return s_TotalDropped;
}

std::uint64_t GetDiagnosticSinkFailureCount()
{
    std::lock_guard<std::mutex> lock(s_AsyncMutex);
    return s_SinkFailures + (s_Collector != nullptr ? s_Collector->GetSinkFailureCount() : 0u);
}

void WriteEmergencyDiagnostic(const char* message) noexcept
{
    if (message != nullptr)
    {
        std::fputs(message, stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
}

DiagnosticSession::DiagnosticSession()
    : m_Active(StartAsyncLogging())
{
}

DiagnosticSession::DiagnosticSession(std::vector<std::unique_ptr<IDiagnosticSink>> sinks)
    : m_Active(StartAsyncLogging(std::move(sinks)))
{
}

DiagnosticSession::~DiagnosticSession()
{
    if (m_Active == true)
    {
        StopAsyncLogging();
    }
}

bool DiagnosticSession::IsActive() const noexcept
{
    return m_Active;
}

void SetDiagnosticsEnabled(bool enabled) noexcept
{
    s_Enabled.store(enabled, std::memory_order_relaxed);
}

bool IsDiagnosticsEnabled() noexcept
{
    return RAVEN_ENABLE_DIAGNOSTICS != 0 && s_Enabled.load(std::memory_order_relaxed);
}

namespace
{
bool Publish(DiagnosticEventId id, DiagnosticCategory category,
    DiagnosticSeverity severity, DiagnosticPayload payload, bool asyncOnly, std::uint64_t sessionId)
{
    NormalizePayload(payload);

    DiagnosticEvent event;
    event.Id = id;
    event.Category = category;
    event.Severity = severity;
    event.TimestampNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    if (s_ThreadBuffer.ThreadId == 0)
    {
        // std::thread::idのhashは衝突やOS ID再利用があり得るため、process内の通算IDを割り当てます。
        s_ThreadBuffer.ThreadId = s_NextThreadId.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    event.ThreadId = s_ThreadBuffer.ThreadId;
    event.Sequence = s_Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.Payload = std::move(payload);

    {
        std::lock_guard<std::mutex> lock(s_AsyncMutex);
        if (asyncOnly == true && (s_Collector == nullptr || sessionId != s_Generation))
        {
            return false;
        }
        if (s_Collector != nullptr)
        {
            // 再起動後に旧スレッドのBufferを再利用しないためGenerationを比較します。
            if (s_ThreadBuffer.Generation != s_Generation)
            {
                if (s_ThreadBuffer.Buffer != nullptr)
                {
                    s_ThreadBuffer.Buffer->Close();
                }
                auto buffer = std::make_shared<EventBuffer>();
                if (s_Collector->RegisterBuffer(buffer) == true)
                {
                    s_ThreadBuffer.Buffer = std::move(buffer);
                    s_ThreadBuffer.Generation = s_Generation;
                }
            }
            if (s_ThreadBuffer.Generation == s_Generation &&
                s_ThreadBuffer.Buffer != nullptr)
            {
                if (s_ThreadBuffer.Buffer->TryPush(std::move(event)) == false)
                {
                    ++s_TotalDropped;
                    return false;
                }
                else
                {
                    s_Collector->Notify();
                }
                return true;
            }
        }
    }

    if (asyncOnly == true)
    {
        return false;
    }

    // Console Sinkの行単位出力を排他し、複数スレッドの文字列混在を防ぎます。
    // Sequenceの採番順と表示順は一致しない場合があります。
    std::lock_guard<std::mutex> lock(s_ConsoleMutex);
    WriteConsoleEvent(std::cout, event);
    return true;
}
}

void Emit(DiagnosticEventId id, DiagnosticCategory category,
    DiagnosticSeverity severity, DiagnosticPayload payload)
{
    if (IsDiagnosticsEnabled() == true)
    {
        Publish(id, category, severity, std::move(payload), false, 0);
    }
}

std::uint64_t GetDiagnosticSessionId()
{
    std::lock_guard<std::mutex> lock(s_AsyncMutex);
    return s_Collector != nullptr ? s_Generation : 0;
}

bool EmitMetric(const DiagnosticMetric& metric, DiagnosticCategory category, std::uint64_t sessionId) noexcept
{
    if (RAVEN_ENABLE_DIAGNOSTICS == 0 || RAVEN_ENABLE_DIAGNOSTIC_MEASUREMENTS == 0
        || sessionId == 0 || metric.IsValid() == false)
    {
        return false;
    }
    const auto id = metric.Kind == DiagnosticMetricKind::Counter ? EventIds::Counter
        : (metric.Kind == DiagnosticMetricKind::TraceBegin ? EventIds::TraceBegin : EventIds::TraceEnd);
    try
    {
        // Scope開始後に無効化されてもEndを閉じます。新規発行の有効判定は計測APIが担当します。
        return Publish(id, category, DiagnosticSeverity::Info, metric, true, sessionId);
    }
    catch (...)
    {
        // 計測失敗がPhysics/RHIの処理やRAII Destructorへ例外を持ち込まないようにします。
        WriteEmergencyDiagnostic("Diagnostics measurement could not be published.");
        return false;
    }
}
}
}
