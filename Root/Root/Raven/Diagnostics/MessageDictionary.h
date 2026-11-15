#pragma once
#include "Raven/Diagnostics/DiagnosticAPI.h"
#include <span>
#include <string_view>

namespace Raven::Diagnostics
{
enum class DiagnosticLanguage { English, Japanese };
enum class MessageFormatStatus { Formatted, UnknownMessage, UnsupportedDictionary, SchemaMismatch, InvalidArguments };
struct MessageArgumentDefinition { std::string_view Name; DiagnosticArgumentType Type; };
struct MessageDefinition
{
    DiagnosticEventId Id;
    std::string_view Key;
    std::uint16_t SchemaVersion;
    std::uint64_t SchemaHash;
    DiagnosticCategory Category;
    DiagnosticSeverity Severity;
    std::span<const MessageArgumentDefinition> Arguments;
    std::string_view English;
    std::string_view Japanese;
};
struct MessageFormatResult { std::string Text; MessageFormatStatus Status; };
std::span<const MessageDefinition> GetMessageDefinitions() noexcept;
const MessageDefinition* FindMessageDefinition(DiagnosticEventId id) noexcept;
bool MatchesMessageSchema(const MessageDefinition& definition, const DiagnosticMessage& message) noexcept;
MessageFormatResult FormatDiagnosticMessage(DiagnosticEventId id, const DiagnosticMessage& message,
    DiagnosticLanguage language = DiagnosticLanguage::English);
// 通常ログ用。Native callbackのnon-blocking経路には利用しません。
bool EmitStructuredMessage(DiagnosticEventId id, DiagnosticMessage message);
bool IsStructuredMessageEnabled(DiagnosticEventId id) noexcept;
}

#if RAVEN_ENABLE_DIAGNOSTICS
#define RAVEN_MESSAGE(id, payload) do { if (::Raven::Diagnostics::IsStructuredMessageEnabled(id) == true) { ::Raven::Diagnostics::EmitStructuredMessage(id, payload); } } while (false)
#else
#define RAVEN_MESSAGE(id, payload) do {} while (false)
#endif
