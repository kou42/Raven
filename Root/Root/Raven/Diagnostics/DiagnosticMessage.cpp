#include "Raven/Diagnostics/DiagnosticMessage.h"
#include <cmath>

namespace Raven::Diagnostics
{
bool DiagnosticMessage::IsValid() const noexcept
{
    if (DictionaryVersion == 0 || SchemaVersion == 0 || SchemaHash == 0 || Arguments.size() > MaxMessageArguments) { return false; }
    std::size_t bytes = 0;
    for (const auto& argument : Arguments)
    {
        if (argument.valueless_by_exception() == true) { return false; }
        if (const auto* text = std::get_if<std::string>(&argument); text != nullptr)
        {
            if (text->size() > MaxMessageArgumentTextBytes) { return false; }
            bytes += text->size();
        }
        else if (const auto* real = std::get_if<double>(&argument); real != nullptr)
        {
            if (std::isfinite(*real) == false) { return false; }
            bytes += 8;
        }
        else { bytes += std::holds_alternative<bool>(argument) ? 1 : 8; }
    }
    return bytes <= MaxMessageArgumentBytes;
}
}
