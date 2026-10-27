#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <iosfwd>
#include <variant>

namespace Raven
{
namespace Diagnostics
{
// IDは将来のMessage DictionaryとBinary Traceで共通使用します。
// 0は未登録を示し、Sequenceとは別の安定したメッセージ識別子です。
using DiagnosticEventId = std::uint64_t;

enum class DiagnosticCategory : std::uint8_t
{
    Core, RHI, Renderer, Physics, Animation, Asset, Runtime
};

enum class DiagnosticSeverity : std::uint8_t
{
    Info, Warning, Error
};

// Phase 01では値を所有するPayloadのみ許可し、非同期化後も参照切れを防ぎます。
// 文字列の最大長は1024バイト（UTF-8の途中切断を避ける処理は実装側）です。
constexpr std::size_t MaxDiagnosticTextBytes = 1024;
using DiagnosticPayload = std::variant<std::int64_t, double, bool, std::string>;

struct DiagnosticEvent
{
    DiagnosticEventId Id = 0;
    DiagnosticCategory Category = DiagnosticCategory::Core;
    DiagnosticSeverity Severity = DiagnosticSeverity::Info;
    std::uint64_t TimestampNanoseconds = 0;
    std::uint64_t ThreadId = 0;
    std::uint64_t Sequence = 0;
    DiagnosticPayload Payload;
};

// Event IDはSubsystemごとに領域を分け、同じ事象には同じIDを使用します。
// 既存イベントのIDは変更せず、将来のDictionary/Binary Traceとの互換性を保ちます。
namespace EventIds
{
constexpr DiagnosticEventId LegacyRHIInitializationFailed = 0x1001u;
constexpr DiagnosticEventId PhysicsGravityUpdated = 0x2001u;
}

// 値所有Payloadの文字列サイズを制限します。EmitとSelf Testで同じ規則を使用します。
void NormalizePayload(DiagnosticPayload& payload);

// 現段階では同期Console出力。後続PhaseでSink/Collectorへ差し替えます。
// IDは呼び出し側で安定した値を指定し、Sequenceは発行順を表します。
// Consoleへの書式化はストリームに分離し、GPU不要のSelf Testから検証できます。
void WriteConsoleEvent(std::ostream& output, const DiagnosticEvent& event);

void Emit(DiagnosticEventId id, DiagnosticCategory category,
    DiagnosticSeverity severity, DiagnosticPayload payload);
}
}

#define RAVEN_LOG_INFO(category, id, payload) \
    ::Raven::Diagnostics::Emit((id), (category), ::Raven::Diagnostics::DiagnosticSeverity::Info, (payload))
#define RAVEN_LOG_WARN(category, id, payload) \
    ::Raven::Diagnostics::Emit((id), (category), ::Raven::Diagnostics::DiagnosticSeverity::Warning, (payload))
#define RAVEN_LOG_ERROR(category, id, payload) \
    ::Raven::Diagnostics::Emit((id), (category), ::Raven::Diagnostics::DiagnosticSeverity::Error, (payload))
