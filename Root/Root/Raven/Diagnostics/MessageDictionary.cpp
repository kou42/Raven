#include "Raven/Diagnostics/MessageDictionary.h"
#include <iomanip>
#include <algorithm>
#include <limits>
#include <locale>
#include <sstream>

namespace Raven::Diagnostics
{
namespace
{
std::string ArgumentText(const DiagnosticArgument& argument)
{
    if (argument.valueless_by_exception() == true) { return "<invalid>"; }
    if (const auto* text = std::get_if<std::string>(&argument); text != nullptr) { return text->substr(0, MaxMessageArgumentTextBytes); }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << std::boolalpha;
    std::visit([&](const auto& value) { output << value; }, argument);
    return output.str();
}
}
const MessageDefinition* FindMessageDefinition(DiagnosticEventId id) noexcept
{
    for (const auto& definition : GetMessageDefinitions()) { if (definition.Id == id) { return &definition; } }
    return nullptr;
}
bool MatchesMessageSchema(const MessageDefinition& definition, const DiagnosticMessage& message) noexcept
{
    if (message.IsValid() == false || message.DictionaryVersion != 1 || definition.SchemaVersion != message.SchemaVersion
        || definition.SchemaHash != message.SchemaHash || definition.Arguments.size() != message.Arguments.size()) { return false; }
    for (std::size_t index = 0; index < message.Arguments.size(); ++index)
    {
        if (static_cast<std::size_t>(definition.Arguments[index].Type) != message.Arguments[index].index() + 1) { return false; }
    }
    return true;
}
MessageFormatResult FormatDiagnosticMessage(DiagnosticEventId id, const DiagnosticMessage& message, DiagnosticLanguage language)
{
    const auto* definition = FindMessageDefinition(id);
    auto status = MessageFormatStatus::Formatted;
    if (message.IsValid() == false) { status = MessageFormatStatus::InvalidArguments; }
    else if (message.DictionaryVersion != 1) { status = MessageFormatStatus::UnsupportedDictionary; }
    else if (definition == nullptr) { status = MessageFormatStatus::UnknownMessage; }
    else if (MatchesMessageSchema(*definition, message) == false) { status = MessageFormatStatus::SchemaMismatch; }
    if (status != MessageFormatStatus::Formatted)
    {
        // 異なる版の引数を現在の文言へ当てはめると誤診につながるため、生値へ退避します。
        const char* reason = status == MessageFormatStatus::UnknownMessage ? "unknown ID"
            : status == MessageFormatStatus::UnsupportedDictionary ? "unsupported dictionary"
            : status == MessageFormatStatus::SchemaMismatch ? "schema mismatch" : "invalid arguments";
        std::string text = std::string("[message unavailable: ") + reason + " id=" + std::to_string(id)
            + " dictionary=" + std::to_string(message.DictionaryVersion) + " schema=" + std::to_string(message.SchemaVersion)
            + " hash=" + std::to_string(message.SchemaHash) + "]";
        for (std::size_t index = 0; index < std::min(message.Arguments.size(), MaxMessageArguments); ++index)
        {
            text += " arg" + std::to_string(index) + ':' + std::to_string(message.Arguments[index].index() + 1) + '='
                + ArgumentText(message.Arguments[index]).substr(0, MaxMessageArgumentTextBytes);
        }
        return {std::move(text), status};
    }
    const auto pattern = language == DiagnosticLanguage::Japanese ? definition->Japanese : definition->English;
    std::string text;
    for (std::size_t cursor = 0; cursor < pattern.size();)
    {
        if (pattern[cursor] != '{') { text += pattern[cursor++]; continue; }
        const auto end = pattern.find('}', cursor + 1);
        if (end == std::string_view::npos) { text += "<invalid template>"; break; }
        const auto name = pattern.substr(cursor + 1, end - cursor - 1);
        for (std::size_t index = 0; index < definition->Arguments.size(); ++index)
        {
            if (definition->Arguments[index].Name == name) { text += ArgumentText(message.Arguments[index]); break; }
        }
        cursor = end + 1;
    }
    return {std::move(text), status};
}
bool IsStructuredMessageEnabled(DiagnosticEventId id) noexcept
{
    const auto* definition = FindMessageDefinition(id);
    return definition != nullptr && IsDiagnosticCategoryEnabled(definition->Category);
}
}
