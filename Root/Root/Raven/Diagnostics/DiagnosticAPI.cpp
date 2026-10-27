#include "Raven/Diagnostics/DiagnosticAPI.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <type_traits>

namespace Raven
{
namespace Diagnostics
{
namespace
{
std::atomic<std::uint64_t> s_Sequence{ 0 };
std::mutex s_ConsoleMutex;

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

void Emit(DiagnosticEventId id, DiagnosticCategory category,
    DiagnosticSeverity severity, DiagnosticPayload payload)
{
    if (auto* text = std::get_if<std::string>(&payload); text != nullptr)
    {
        LimitText(*text);
    }

    DiagnosticEvent event;
    event.Id = id;
    event.Category = category;
    event.Severity = severity;
    event.TimestampNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    event.ThreadId = static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    event.Sequence = s_Sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    event.Payload = std::move(payload);

    // Console Sinkの行単位出力を排他し、複数スレッドの文字列混在を防ぎます。
    // Sequenceの採番順と表示順は一致しない場合があります。
    std::lock_guard<std::mutex> lock(s_ConsoleMutex);
    std::cout << "[Diagnostics][" << SeverityName(event.Severity) << "]["
        << CategoryName(event.Category) << "] id=" << event.Id
        << " seq=" << event.Sequence << " ts_ns=" << event.TimestampNanoseconds
        << " thread=" << event.ThreadId << " payload=";
    std::visit([](const auto& value) { std::cout << value; }, event.Payload);
    std::cout << '\n';
}
}
}
