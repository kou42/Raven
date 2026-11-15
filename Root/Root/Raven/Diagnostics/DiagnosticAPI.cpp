#include "Raven/Diagnostics/DiagnosticAPI.h"
#include "Raven/Diagnostics/MessageDictionary.h"
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
std::atomic<std::uint32_t> s_CategoryMask{ (1u << DiagnosticCategoryCount) - 1u };
std::mutex s_ConsoleMutex;
std::mutex s_AsyncMutex;
std::shared_ptr<DiagnosticCollector> s_Collector;
std::uint64_t s_Generation = 0;
std::atomic<std::uint64_t> s_TotalDropped{0};
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
    case DiagnosticCategory::SoftBody: return "SoftBody";
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
    else if (const auto* validation = std::get_if<DiagnosticValidation>(&event.Payload); validation != nullptr)
    {
        output << "backend=" << static_cast<unsigned>(validation->Backend)
            << " native_id=" << validation->MessageId << " native_severity=" << validation->NativeSeverity
            << " native_type=" << validation->NativeType << " frame=" << validation->FrameId
            << " truncated=" << (validation->NameTruncated || validation->MessageTruncated)
            << " name=" << validation->Name << " message=" << validation->Message;
    }
    else if (const auto* message = std::get_if<DiagnosticMessage>(&event.Payload); message != nullptr)
    {
        output << FormatDiagnosticMessage(event.Id, *message).Text;
    }
    else
    {
        // 計測型は上で処理し、従来Logの書式は維持します。
        std::visit([&output](const auto& value)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, DiagnosticMetric> == false
                && std::is_same_v<std::decay_t<decltype(value)>, DiagnosticValidation> == false
                && std::is_same_v<std::decay_t<decltype(value)>, DiagnosticMessage> == false)
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
    // Sink Workerの終了joinとSession制御mutexの相互待機を作らず、累積値だけを観測します。
    return s_TotalDropped.load(std::memory_order_relaxed);
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

void SetDiagnosticCategoryEnabled(DiagnosticCategory category, bool enabled) noexcept
{
    const auto index = static_cast<unsigned>(category);
    if (index >= DiagnosticCategoryCount)
    {
        return;
    }
    const auto bit = 1u << index;
    if (enabled == true)
    {
        s_CategoryMask.fetch_or(bit, std::memory_order_relaxed);
    }
    else
    {
        s_CategoryMask.fetch_and(~bit, std::memory_order_relaxed);
    }
}

bool IsDiagnosticCategoryEnabled(DiagnosticCategory category) noexcept
{
    const auto index = static_cast<unsigned>(category);
    return IsDiagnosticsEnabled() && index < DiagnosticCategoryCount
        && (s_CategoryMask.load(std::memory_order_relaxed) & (1u << index)) != 0;
}

namespace
{
bool Publish(DiagnosticEventId id, DiagnosticCategory category,
    DiagnosticSeverity severity, DiagnosticPayload payload, bool asyncOnly, std::uint64_t sessionId,
    bool nonBlocking = false)
{
    NormalizePayload(payload);
    if (const auto* message = std::get_if<DiagnosticMessage>(&payload); message != nullptr && message->IsValid() == false) { return false; }

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
        std::unique_lock<std::mutex> lock(s_AsyncMutex, std::defer_lock);
        if (nonBlocking == true)
        {
            if (lock.try_lock() == false)
            {
                return false;
            }
        }
        else
        {
            lock.lock();
        }
        if (asyncOnly == true && (s_Collector == nullptr || (sessionId != 0 && sessionId != s_Generation)))
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
                if (s_Collector->RegisterBuffer(buffer, nonBlocking) == true)
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
    if (IsDiagnosticCategoryEnabled(category) == true)
    {
        Publish(id, category, severity, std::move(payload), false, 0);
    }
}

bool EmitStructuredMessage(DiagnosticEventId id, DiagnosticMessage message)
{
    const auto* definition = FindMessageDefinition(id);
    if (definition == nullptr || IsDiagnosticCategoryEnabled(definition->Category) == false
        || MatchesMessageSchema(*definition, message) == false) { return false; }
    return Publish(id, definition->Category, definition->Severity, std::move(message), false, 0);
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
    if (metric.Kind != DiagnosticMetricKind::TraceEnd && IsDiagnosticCategoryEnabled(category) == false)
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

bool TryEmitValidation(DiagnosticSeverity severity, DiagnosticValidation validation) noexcept
{
    if (IsDiagnosticCategoryEnabled(DiagnosticCategory::RHI) == false || validation.IsValid() == false)
    {
        return false;
    }
    const auto id = validation.Backend == DiagnosticValidationBackend::Vulkan
        ? EventIds::VulkanValidation : EventIds::DX12Validation;
    try
    {
        return Publish(id, DiagnosticCategory::RHI, severity, std::move(validation), true, 0, true);
    }
    catch (...)
    {
        return false;
    }
}
}
}
