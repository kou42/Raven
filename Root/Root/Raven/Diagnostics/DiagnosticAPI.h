#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <iosfwd>
#include <variant>
#include <memory>
#include <vector>

namespace Raven
{
namespace Diagnostics
{
class IDiagnosticSink;
// IDは将来のMessage DictionaryとBinary Traceで共通使用します。
// 0は未登録を示し、Sequenceとは別の安定したメッセージ識別子です。
using DiagnosticEventId = std::uint64_t;

enum class DiagnosticCategory : std::uint8_t
{
    Core, RHI, Renderer, Physics, Animation, Asset, Runtime
};
constexpr std::uint32_t DiagnosticCategoryCount = static_cast<std::uint32_t>(DiagnosticCategory::Runtime) + 1u;

enum class DiagnosticSeverity : std::uint8_t
{
    Info, Warning, Error
};

// Phase 01では値を所有するPayloadのみ許可し、非同期化後も参照切れを防ぎます。
// 単一の数値または文字列を保持します。文字列の最大保持長は1024バイトです。
// 文字列入力は正しいUTF-8を前提とし、上限での途中切断を避ける処理は実装側です。
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

// 非同期経路は明示的に開始し、停止時は同期Consoleへ戻します。
// StopAsyncLoggingはすべてのProducerが終了した後に呼んでください。
// 既存の同期Console動作を既定値として維持します。
bool StartAsyncLogging();
// Sinkの所有権を移譲します。開始失敗時も渡したSinkは破棄されます。
bool StartAsyncLogging(std::vector<std::unique_ptr<IDiagnosticSink>> sinks);
void FlushDiagnostics();
void StopAsyncLogging();
std::uint64_t GetAsyncDroppedCount();
std::uint64_t GetDiagnosticSinkFailureCount();

// Queue/Collectorのロックを取得しないbest-effort stderr出力です。
// 通常の致命的エラー経路向けであり、signal handlerや壊れたprocessでの保存保証はありません。
void WriteEmergencyDiagnostic(const char* message) noexcept;

// 実行中のProducerを先に破棄するため、Application等より前に生成してください。
// 既に他のSessionが開始済みなら所有せず、終了時もそのSessionを停止しません。
// 所有Sessionの生存中は手動Stop/Startを混在させないでください。
class DiagnosticSession final
{
public:
    DiagnosticSession();
    explicit DiagnosticSession(std::vector<std::unique_ptr<IDiagnosticSink>> sinks);
    ~DiagnosticSession();
    DiagnosticSession(const DiagnosticSession&) = delete;
    DiagnosticSession& operator=(const DiagnosticSession&) = delete;
    bool IsActive() const noexcept;

private:
    bool m_Active = false;
};

// ビルド時無効化に加え、実行時無効化でもマクロのPayload式を評価しません。
void SetDiagnosticsEnabled(bool enabled) noexcept;
bool IsDiagnosticsEnabled() noexcept;

// 未開始時は同期Console出力。
// IDは呼び出し側で安定した値を指定し、Sequenceは発行順を表します。
// Consoleへの書式化はストリームに分離し、GPU不要のSelf Testから検証できます。
void WriteConsoleEvent(std::ostream& output, const DiagnosticEvent& event);

void Emit(DiagnosticEventId id, DiagnosticCategory category,
    DiagnosticSeverity severity, DiagnosticPayload payload);
}
}

#ifndef RAVEN_ENABLE_DIAGNOSTICS
#define RAVEN_ENABLE_DIAGNOSTICS 1
#endif

#if RAVEN_ENABLE_DIAGNOSTICS
#define RAVEN_DIAGNOSTIC_LOG(category, id, severity, payload) \
    do { if (::Raven::Diagnostics::IsDiagnosticsEnabled() == true) { \
        ::Raven::Diagnostics::Emit((id), (category), (severity), (payload)); } } while (false)
#else
#define RAVEN_DIAGNOSTIC_LOG(category, id, severity, payload) do {} while (false)
#endif
#define RAVEN_LOG_INFO(category, id, payload) \
    RAVEN_DIAGNOSTIC_LOG(category, id, ::Raven::Diagnostics::DiagnosticSeverity::Info, payload)
#define RAVEN_LOG_WARN(category, id, payload) \
    RAVEN_DIAGNOSTIC_LOG(category, id, ::Raven::Diagnostics::DiagnosticSeverity::Warning, payload)
#define RAVEN_LOG_ERROR(category, id, payload) \
    RAVEN_DIAGNOSTIC_LOG(category, id, ::Raven::Diagnostics::DiagnosticSeverity::Error, payload)
